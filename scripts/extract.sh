#!/bin/bash
set -e

NO_LIRC=""
for arg in "$@"; do
  case "$arg" in
    --no-lirc) NO_LIRC="1" ;;
  esac
done

if [[ ! -d "package-sources" ]]; then
  echo "Error: Run from repository root"
  exit 1
fi

if [[ ! -f "package-sources/r_attenu.c" ]]; then
  echo "Error: package-sources/r_attenu.c not found"
  exit 1
fi

DEST_DIR="build/rattenu/source"

echo "[+] Cleaning $DEST_DIR"
rm -rf "$DEST_DIR"
mkdir -p "$DEST_DIR"

echo "[+] Copying source files"
cp package-sources/r_attenu.c "$DEST_DIR/"
cp package-sources/r_attenuc.c "$DEST_DIR/"

echo "[+] Creating debian packaging for fooNerd"

cd "$DEST_DIR"
mkdir -p debian

# Create Makefile
cat > Makefile << 'EOFMAKE'
DEB_HOST_MULTIARCH ?= $(shell dpkg-architecture -qDEB_HOST_MULTIARCH)

LGPIO_CFLAGS := $(shell pkg-config --cflags libfn-lgpio 2>/dev/null)
LGPIO_LIBS := $(shell pkg-config --libs libfn-lgpio 2>/dev/null)

# Fallback if pkg-config not available
ifeq ($(LGPIO_LIBS),)
  LGPIO_LIBS := -L/usr/lib/$(DEB_HOST_MULTIARCH) -lfn-lgpio
endif

LIRC_LIBS :=
LIRC_CFLAGS :=
ifndef NO_LIRC
  LIRC_LIBS := -llirc_client
else
  LIRC_CFLAGS := -DNO_LIRC
endif

CC ?= gcc
CFLAGS ?= -O2
CFLAGS += -Wall $(LGPIO_CFLAGS) $(LIRC_CFLAGS)

all: fn-rattenu fn-rattenuc

fn-rattenu: r_attenu.c
	$(CC) $(CFLAGS) -o $@ $< $(LGPIO_LIBS) $(LIRC_LIBS) -lpthread

fn-rattenuc: r_attenuc.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f fn-rattenu fn-rattenuc

install: all
	install -d $(DESTDIR)/usr/bin
	install -m 755 fn-rattenu $(DESTDIR)/usr/bin/
	install -m 755 fn-rattenuc $(DESTDIR)/usr/bin/

.PHONY: all clean install
EOFMAKE

# Create debian/control
if [[ -n "$NO_LIRC" ]]; then
  LIRC_DEPENDS=""
else
  LIRC_DEPENDS="liblircclient0,"
fi

cat > debian/control << EOF
Source: foonerd-rattenu
Section: misc
Priority: optional
Maintainer: fooNerd (Just a Nerd) <nerd@foonerd.com>
Build-Depends: debhelper (>= 10~),
               libfn-lgpio-dev,
               liblircclient-dev,
               pkg-config
Standards-Version: 4.1.4
Homepage: https://github.com/foonerd/allo-relay-builds

Package: foonerd-rattenu
Architecture: any
Depends: ${LIRC_DEPENDS}
         \${misc:Depends},
         \${shlibs:Depends}
Description: fooNerd Allo Relay Attenuator tools
 Volume control daemon and client for Allo Relay Attenuator hardware.
 Custom build by fooNerd for Volumio integration.
 Ported from WiringPi to lgpio for Bookworm compatibility.
 .
 Binaries:
  * fn-rattenu: Volume control daemon
  * fn-rattenuc: Command-line client
EOF

# Create debian/rules
cat > debian/rules << 'EOFRULES'
#!/usr/bin/make -f
DEB_HOST_MULTIARCH ?= $(shell dpkg-architecture -qDEB_HOST_MULTIARCH)
export DEB_HOST_MULTIARCH

%:
	dh $@

override_dh_auto_build:
ifdef NO_LIRC
	$(MAKE) NO_LIRC=1
else
	$(MAKE)
endif

override_dh_auto_install:
	$(MAKE) DESTDIR=$(CURDIR)/debian/foonerd-rattenu install

override_dh_install:
	# Skip - files installed directly to package dir by override_dh_auto_install

override_dh_fixperms:
	dh_fixperms || true

override_dh_shlibdeps:
	dh_shlibdeps --dpkg-shlibdeps-params=--ignore-missing-info || true
EOFRULES

chmod +x debian/rules

# Create debian/compat
echo "10" > debian/compat

# Note: No .install file needed - override_dh_auto_install installs directly to package dir

# Create changelog
cat > debian/changelog << 'EOF'
foonerd-rattenu (2.0.1-1) bookworm; urgency=medium

  * Replace GPIO interrupt with polling-based button detection
  * Works on all Pi variants regardless of GPIO chip number
  * Add play/pause button support via volumio toggle command
  * Graceful handling when volumio binary not available

 -- fooNerd (Just a Nerd) <nerd@foonerd.com>  Wed, 25 Dec 2024 16:00:00 +0000

foonerd-rattenu (2.0.0-1) bookworm; urgency=medium

  * Initial fooNerd release
  * Ported from WiringPi to lgpio
  * Binary prefix: fn-*
  * For Volumio Allo Relay Attenuator plugin

 -- fooNerd (Just a Nerd) <nerd@foonerd.com>  Sat, 23 Nov 2024 10:00:00 +0000
EOF

# Create source/format
mkdir -p debian/source
echo "3.0 (native)" > debian/source/format

cd - > /dev/null

echo "[OK] Source prepared in $DEST_DIR"
