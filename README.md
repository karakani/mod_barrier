# mod_barrier
apache module to authenticate an access from known bots

This is very early release. Not well tested. Please be careful to use on production
environment.

## requirement

* Latest apache httpd 2.4 running on CentOS 7. I've not tested other platforms and versions.
* Appropriate mod_socache_xxxx module. In most cases, you don't have to do anything from default installation. See https://httpd.apache.org/docs/2.4/en/socache.html for detail.

It may be easy to build other platforms by editing Makefile or module.mk.

This module may run on older httpd 2.4 release, but I'm not sure for httpd 2.2.

## build

Just do `make`.

## install

Do `sudo make install`

Don't forget restart httpd after installation and configuration.

## configuration

Following sample is a minimum configuration.

~~~
LoadModule  barrier_module  modules/mod_barrier.so

# https://support.google.com/webmasters/answer/80553?hl=en
BrowserMatch Googlebot UA_BARRIER_CHECK=.googlebot.com,.google.com

# https://www.bing.com/webmaster/help/how-to-verify-bingbot-3905dc26
BrowserMatch bingbot   UA_BARRIER_CHECK=.search.msn.com

# http://www.ysearchblog.com/2007/06/05/yahoo-search-crawler-slurp-has-a-new-address-and-signature-card/
BrowserMatch 'Yahoo! Slurp' UA_BARRIER_CHECK=.crawl.yahoo.net

BarrierBlockExpire  20
BarrierSOCache      shmcb
~~~

### parameters

BrowserMatch
: This is BrowserMatch what you know. This module runs with 'UA_BARRIER_CHECK' variable. This is hard coded variable name at this moment. You can set multiple domains separated by comma. Domains can be specified two forms.
: - One: partial match. specify host name with leading dot(see configuration sample above). host name must end with this host name. 
: - Two: full match. specify full host name WITHOUT leading dot. This may be useful if you are operating your own bot for some purpose.
: See apache httpd official document for BrowserMatch directive at https://httpd.apache.org/docs/2.4/mod/mod_setenvif.html

BarrierBlockExpire
: Cache check result and block remote host for this time in seconds.

BarrierSOCache
: Cache mechanism. You need to specify socache modules. You will be ok by choosing `shmcb` for this parameter for most cases. See https://httpd.apache.org/docs/2.4/en/socache.html for detail.

