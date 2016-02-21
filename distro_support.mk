# Support make file for various Linux distributions.
# These codes are borrowed from https://gist.github.com/paoneJP/cfa8650e20b33e4506d4
# Details are available at https://paonejp.github.io/2015/04/16/building_apache_module_on_various_linux_distro.html
# (Japanese document)

module_name=$(patsubst mod_%,%,$(shared:%.la=%))

ifneq ($(wildcard /etc/debian_version),)
  # Debian/Ubuntu
  top_srcdir=/usr/share/apache2
  top_builddir=/usr/share/apache2
  include /usr/share/apache2/build/special.mk
else ifneq ($(wildcard /etc/redhat-release),)
  # RHEL/CentOS
  top_srcdir=/etc/httpd
  ifneq ($(wildcard /usr/lib64),)  # 64bit
    top_builddir=/usr/lib64/httpd
    include /usr/lib64/httpd/build/special.mk
  else
    top_builddir=/usr/lib/httpd
    include /usr/lib/httpd/build/special.mk
  endif
else
  $(error unsupported platform)
endif

