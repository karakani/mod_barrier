/* 
**  mod_barrier.c
**  version: 1.0.0
**
**  Copyright 2016 Toshihiro Karakane
**
**  Licensed under the Apache License, Version 2.0 (the "License");
**  you may not use this file except in compliance with the License.
**  You may obtain a copy of the License at
**
**      http://www.apache.org/licenses/LICENSE-2.0
**
**  Unless required by applicable law or agreed to in writing, software
**  distributed under the License is distributed on an "AS IS" BASIS,
**  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
**  See the License for the specific language governing permissions and
**  limitations under the License.
*/ 

#include "httpd.h"
#include "http_core.h"
#include "http_config.h"
#include "http_protocol.h"
#include "http_request.h"
#include "http_log.h"
#include "ap_socache.h"
#include "ap_config.h"
#include "apr_strings.h"

#define VAL_BANNED	"b"
#define VAL_TRUSTED "t"

#define STAT_UNKNOWN 0
#define STAT_TRUSTED 1
#define STAT_BANNED  2

#define HNCK_SUCCESS  0
#define HNCK_SOFTFAIL 1
#define HNCK_HARDFAIL 2

#define CACHE_NAME "mod_barrier"

#define LIST_DELIMITER ","

static void soft_ban(request_rec *r);
/**
 * mark a remote host banned
 */
static void ban(request_rec *r);
/**
 * mark a remote host trust
 */
static void trust(request_rec *r);
static int check_hostname(request_rec *r, const char *remote_ip, const char* resolved_host, const char *expected_domain);
/**
 * check a remote host trusted or banned
 */
static int address_test(request_rec *r);
static apr_status_t destroy_cache(void *data);
static const char *resolve_host_from_address(apr_sockaddr_t *sa);
static apr_sockaddr_t *resolve_address_from_host(apr_pool_t *p, const char *host);

static ap_socache_provider_t* socache_provider;
/**
 * cache instance.
 * data is stored in following format
 *
 * key:value = "ip address":"const char *"
 */
static ap_socache_instance_t *socache_instance;

static int cache_expire = 0;

/**
 * main handler.
 */
static int barrier_handler(request_rec *r, int lookup_uri) {
	const char *expected_domains;
	const char* remote_ip = r->useragent_ip;

	int address_cache;

	// Should we check remote host??
	// This module uses environment variable for ua identification.
	// It should be efficient way to use existing (and commonly used)
	// header check process.
	expected_domains = apr_table_get(r->subprocess_env, "UA_BARRIER_CHECK");

	// return if it's other than crawlers.
	if (expected_domains == NULL) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "No check is required: %s", remote_ip);
		return DECLINED;
	} else if (!strcmp(expected_domains, "1")) {
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "Hostname(s) is not set. May be misconfiguration. No test will be run.");
		return DECLINED;
	}

	address_cache = address_test(r);
	if (address_cache) {
		// if result found in cache
		switch (address_cache) {
		case STAT_BANNED:
			ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "remote address is banned: %s", remote_ip);
			return HTTP_FORBIDDEN;
		case STAT_TRUSTED:
			ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "remote address is trusted: %s", remote_ip);
			return DECLINED;
		}
	}

	// check double reverse lookup.
	const char *remote_host;
	apr_sockaddr_t* remote_sock;

	// test forward proxy
	remote_host = resolve_host_from_address(r->useragent_addr);
	if (remote_host == NULL) {
		soft_ban(r);
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "reverse lookup failed (soft fail): %s", remote_ip);
		return HTTP_FORBIDDEN;
	}

	remote_sock = resolve_address_from_host(r->pool, remote_host);

	// return forbidden if reverse double lookup is failed.
	if (remote_sock == NULL || !apr_sockaddr_equal(r->useragent_addr, remote_sock)) {
		soft_ban(r);
		ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "double reverse lookup failed (soft fail): %s", remote_ip);
		return HTTP_FORBIDDEN;
	}

	ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "hostname retained: %s/%s. Going to match against domain names: %s", remote_ip, remote_host, expected_domains);

	char* last;
	char* domain;
	char* domains = apr_pstrdup(r->pool, expected_domains);

	// check for all domains.
	domain = apr_strtok(domains, ",", &last);
	while (domain) {
		ap_log_rerror(APLOG_MARK, APLOG_DEBUG, 0, r, "testing domain '%s' to '%s/%s'", domain, remote_ip, remote_host);
		// Once check_hostname returns DECLINED once, return DECLINED.
		if (check_hostname(r, remote_ip, remote_host, (const char*)domain) == HNCK_SUCCESS) {
			trust(r);
			ap_log_rerror(APLOG_MARK, APLOG_INFO, 0, r, "domain matched (domain=%s): %s/%s", domain, remote_ip, remote_host);
			return DECLINED;
		}
		domain = apr_strtok(NULL, ",", &last);
	}

	// If check_hostname never returns DECLINED, return HTTP_FORBIDDEN
	ban(r);
	ap_log_rerror(APLOG_MARK, APLOG_WARNING, 0, r, "double reverse lookup result is unmatched: %s/%s (expected: %s)", remote_ip, remote_host, expected_domains);
	return HTTP_FORBIDDEN;
}

static int check_hostname(request_rec *r, const char *remote_ip, const char* resolved_host, const char *expected_domain) {
	// Check remote host name and expected domain.
	// remote_host must contains remote_domain at its end.
	const char *pos;
	pos = strstr(resolved_host, expected_domain);
	if (pos == NULL) {
		return HNCK_HARDFAIL;
	}

	if (expected_domain[0] == '.') {
		if (strlen(expected_domain) + (pos - resolved_host) == strlen(resolved_host)) {
			return HNCK_SUCCESS;
		} else {
			return HNCK_HARDFAIL;
		}
	} else {
		if (!strcmp(resolved_host, expected_domain)) {
			return HNCK_SUCCESS;
		} else {
			return HNCK_HARDFAIL;
		}
	}
}

static const char *resolve_host_from_address(apr_sockaddr_t *sa) {
	char *host;
	apr_status_t rv;

	rv = apr_getnameinfo(&host, sa, 0);

	return rv == APR_SUCCESS ? (const char *) host : NULL;
}

static apr_sockaddr_t *resolve_address_from_host(apr_pool_t *p, const char *host) {
	char *ip;
	apr_status_t rv;
	apr_sockaddr_t *sa, *sa2;

	rv = apr_sockaddr_info_get(&sa, host, APR_UNSPEC, 0, 0, p);
	if (rv != APR_SUCCESS)
		return NULL;

	rv = apr_sockaddr_ip_get(&ip, sa);
	if (rv != APR_SUCCESS)
		return NULL;

	rv = apr_sockaddr_info_get(&sa2, ip, APR_UNSPEC, 0, 0, p);
	if (rv != APR_SUCCESS)
		return NULL;

	return sa2;
}

static void soft_ban(request_rec *r) {
	const char *value = VAL_BANNED;
	const char* remote = r->useragent_ip;

	if (cache_expire < 1) return;

	socache_provider->store(socache_instance, r->server,
			(unsigned char *) remote, strlen(remote),
			apr_time_now() + (apr_time_from_sec(cache_expire) / 10),
			(unsigned char *) value, strlen(value),
			r->pool);
}

static void ban(request_rec *r) {
	const char *value = VAL_BANNED;
	const char* remote = r->useragent_ip;

	if (cache_expire < 1) return;

	socache_provider->store(socache_instance, r->server,
			(unsigned char *) remote, strlen(remote),
			apr_time_now() + apr_time_from_sec(cache_expire),
			(unsigned char *) value, strlen(value),
			r->pool);
}

static void trust(request_rec *r) {
	const char *value = VAL_TRUSTED;
	const char* remote = r->useragent_ip;

	if (cache_expire < 1) return;

	socache_provider->store(socache_instance, r->server,
			(unsigned char *) remote, strlen(remote),
			apr_time_now() + apr_time_from_sec(cache_expire),
			(unsigned char *) value, strlen(value),
			r->pool);
}

static int address_test(request_rec *r) {
	unsigned int valueSize = 100;
	unsigned char value[valueSize];
	apr_status_t rv;

	if (cache_expire < 1) {
		return STAT_UNKNOWN;
	}

	const char* remote = r->useragent_ip;

	rv = socache_provider->retrieve(socache_instance, r->server,
			(unsigned char *) remote, strlen(remote), (unsigned char*)value, &valueSize, r->pool);

	if (rv == APR_SUCCESS) {
		value[valueSize] = 0;	// NULL termination
		if (!strcmp((const char *)value, VAL_TRUSTED)) {
			return STAT_TRUSTED;
		} else {
			return STAT_BANNED;
		}
	} if (APR_STATUS_IS_NOTFOUND(rv)) {
		return STAT_UNKNOWN;
	} else {
		return STAT_UNKNOWN;
	}
}

static int barrier_post_config(apr_pool_t *pconf, apr_pool_t *plog, apr_pool_t *ptmp, server_rec *s) {
	static struct ap_socache_hints cache_hints = {12, 8, apr_time_from_sec(60)};

	if (cache_expire < 1) {
		ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, plog, "No cache is used.");
		return OK;
	}
	ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, plog, "Cache duration: %d sec", cache_expire);

	if (socache_provider == NULL) {
		ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, plog, "No cache provider is specified with BarrierSOCache. Using default one.");
		socache_provider = ap_lookup_provider(
				AP_SOCACHE_PROVIDER_GROUP,
				AP_SOCACHE_DEFAULT_PROVIDER,
				AP_SOCACHE_PROVIDER_VERSION);

		if (socache_provider == NULL) {
			ap_log_perror(APLOG_MARK, APLOG_CRIT, 0, plog, "No socache provider found!!!!!!!!");
			return 500;
		}

		// create instance
		socache_provider->create(&socache_instance, NULL, ptmp, pconf);
	}

	if (socache_instance) {
		ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, plog, "cache initialized");
	} else {
		ap_log_perror(APLOG_MARK, APLOG_NOTICE, 0, plog, "cache initialization failed");
		return 500;
	}

	apr_status_t rv;
	rv = socache_provider->init(socache_instance, CACHE_NAME, &cache_hints, s, pconf);
	if (rv != APR_SUCCESS) {
		ap_log_perror(APLOG_MARK, APLOG_CRIT, 0, plog, "Cache initialization failed: " CACHE_NAME);
		return 500;
	}

	// Register memory clean-up handler
	apr_pool_cleanup_register(pconf, (void *) s, destroy_cache, apr_pool_cleanup_null);

	return OK;
}

static apr_status_t destroy_cache(void *s) {
	if (socache_instance) {
		socache_provider->destroy(socache_instance, (server_rec*) s);
		socache_instance = NULL;
	}
	return APR_SUCCESS;
}

static const char *barriser_set_block_expire(cmd_parms *cmd, void *cfg, const char *arg) {
	const char *errmsg = ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE);

	if (errmsg)
		return errmsg;

	int sec = atoi(arg);
	if (sec > 0) {
		cache_expire = sec;
	} else {
		return "You cannot set other than numeric string for BarrierBlockExpire.";
	}

	return NULL;
}

static const char *barrier_init_cache(cmd_parms *cmd, void *cfg, const char *arg) {
	const char *errmsg = ap_check_cmd_context(cmd, NOT_IN_DIR_LOC_FILE);
	const char *separator, *cache_name;

	if (errmsg)
		return errmsg;

	if (socache_provider)
		// launch fatal error if cache provider is already set.
		return "You cannot set multiple BarrierSOCache";

	separator = ap_strchr(arg, ':');
	if (separator) {
		cache_name = apr_pstrndup(cmd->pool, arg, separator - arg);
	} else {
		cache_name = arg;
	}

	socache_provider = ap_lookup_provider(
			AP_SOCACHE_PROVIDER_GROUP,
			cache_name,
			AP_SOCACHE_PROVIDER_VERSION
	);

	if (socache_provider == NULL) {
			return apr_psprintf(cmd->pool,
					"Unknown socache provider '%s'. Maybe you need "
					"to load the appropriate socache module "
					"(mod_socache_%s?)", cache_name, cache_name
			);
	}

	// create instance
	socache_provider->create(&socache_instance, separator, cmd->temp_pool, cmd->pool);

	return errmsg;
}

static void barrier_register_hooks(apr_pool_t *p) {
	ap_hook_post_config(barrier_post_config, NULL, NULL, APR_HOOK_MIDDLE);
	ap_hook_quick_handler(barrier_handler, NULL, NULL, APR_HOOK_MIDDLE);
}

static const command_rec directives[] =
{
    AP_INIT_TAKE1("BarrierBlockExpire", barriser_set_block_expire, NULL, RSRC_CONF, "Set block expiration"),
    AP_INIT_TAKE1("BarrierSOCache", barrier_init_cache, NULL, RSRC_CONF, "Select cache mechanism and options"),
    { NULL }
};

/* Dispatch list for API hooks */
AP_DECLARE_MODULE(barrier) = {
    STANDARD20_MODULE_STUFF, 
    NULL,                  /* create per-dir    config structures */
    NULL,                  /* merge  per-dir    config structures */
    NULL,                  /* create per-server config structures */
    NULL,                  /* merge  per-server config structures */
	directives,            /* table of config file commands       */
    barrier_register_hooks /* register hooks                      */
};
