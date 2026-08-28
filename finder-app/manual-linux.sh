#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO (DONE): Add your kernel build steps here
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j4 ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} all
    # make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=arm64 CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
cp "${OUTDIR}/linux-stable/arch/arm64/boot/Image" "${OUTDIR}/"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO (DONE): Create necessary base directories
mkdir -p "${OUTDIR}/rootfs"
cd "${OUTDIR}/rootfs"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/lib usr/sbin var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO (DONE):  Configure busybo
    make distclean
    make defconfig
else
    cd busybox
fi


# TODO (DONE): Make and install busybox


make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX="${OUTDIR}/rootfs" ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install


echo "Library dependencies"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "program interpreter"
${CROSS_COMPILE}readelf -a "${OUTDIR}/rootfs/bin/busybox" | grep "Shared library"

# TODO (DONE) (done): Add library dependencies to rootfs
# get the location of the cross compiler from the locationi of the gcc executable
# GCC_DIR=$(type -p ${CROSS_COMPILE}gcc | xargs dirname | xargs dirname)
GCC_DIR=$(${CROSS_COMPILE}gcc --print-sysroot)
cd "${OUTDIR}/rootfs"
#cp ${GCC_DIR}/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 lib/
#cp ${GCC_DIR}/aarch64-none-linux-gnu/libc/lib64/libm.so.6 lib64/
#cp ${GCC_DIR}/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 lib64/
#cp ${GCC_DIR}/aarch64-none-linux-gnu/libc/lib64/libc.so.6 lib64/

cp ${GCC_DIR}/lib/ld-linux-aarch64.so.1 lib/
cp ${GCC_DIR}/lib64/libm.so.6 lib64/
cp ${GCC_DIR}/lib64/libresolv.so.2 lib64/
cp ${GCC_DIR}/lib64/libc.so.6 lib64/

# TODO (DONE): Make device nodes
cd "${OUTDIR}/rootfs"
sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 666 dev/console c 5 1

# TODO (DONE): Clean and build the writer utility
cd "${FINDER_APP_DIR}"
CROSS_COMPILE=${CROSS_COMPILE} make clean
CROSS_COMPILE=${CROSS_COMPILE} make all

# TODO (DONE): Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cd "${FINDER_APP_DIR}"
cp -r writer *.sh ../conf/ "${OUTDIR}/rootfs/home/"

# TODO (DONE): Chown the root directory
sudo chown -R root:root "${OUTDIR}/rootfs" 

# TODO (DONE): Create initramfs.cpio.gz
cd "${OUTDIR}/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio