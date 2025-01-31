AC_PREREQ([2.61])

# package version number (not shared library version)
# odd micro numbers indicate in-progress development
# even micro numbers indicate released versions
m4_define(otiKioskDemo_version_major, A)
m4_define(otiKioskDemo_version_minor, B)
m4_define(otiKioskDemo_version_micro, C)

m4_define([otiKioskDemo_version],
          [otiKioskDemo_version_major.otiKioskDemo_version_minor.otiKioskDemo_version_micro])
m4_define([otiKioskDemo_api_version],
          [otiKioskDemo_version_major.otiKioskDemo_version_minor])

AC_INIT([otiKioskDemo],[otiKioskDemo_version])
AM_INIT_AUTOMAKE([subdir-objects])
AC_CONFIG_HEADERS(config.h)
AC_CONFIG_MACRO_DIRS([m4])
# Checks for programs.
LT_INIT
AM_PROG_CC_C_O
AC_PROG_CXX
AC_PROG_INSTALL

AC_SUBST(otiKioskDemo_CFLAGS)
AC_SUBST(otiKioskDemo_LIBS)

AC_CONFIG_FILES([
Makefile
libotikiosk/Makefile
demo/Makefile
])
AC_OUTPUT

echo ""
echo "   otiKioskDemo $VERSION"
echo "   ====================="
echo ""
echo "   To build the project, run \"make\""
echo ""
