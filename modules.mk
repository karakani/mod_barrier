mod_barrier.la: mod_barrier.slo
	$(SH_LINK) -rpath $(libexecdir) -module -avoid-version  mod_barrier.lo
DISTCLEAN_TARGETS = modules.mk
shared =  mod_barrier.la
