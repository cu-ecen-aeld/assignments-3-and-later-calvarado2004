#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.1.10
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

    # TODO: Add your kernel build steps here
    # Kernel build steps
    # make clean
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} defconfig
    make -j$(nproc) ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # no building modules for assigment 3 part 2
    # make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs

fi

echo "Adding the Image in outdir"
cp -rp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}/.

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p ${OUTDIR}/rootfs
cd ${OUTDIR}/rootfs
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp usr var
mkdir -p usr/bin usr/sbin
mkdir -p var/log

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} CONFIG_PREFIX=${OUTDIR}/rootfs install

echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
ALIB=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter" | awk '{print $NF}' | sed -e 's/\(.*\)]/\1/')
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot)
sudo cp -p "${SYSROOT}${ALIB}" "${OUTDIR}/rootfs/lib64/"
sudo cp -p "${SYSROOT}${ALIB}" "${OUTDIR}/rootfs/lib/"

LIBS=$(${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library" | awk '{print $NF}' | sed -e 's/\[\(.*\)\]/\1/')
for LIB in ${LIBS}; do
    sudo cp -p "${SYSROOT}/lib64/${LIB}" "${OUTDIR}/rootfs/lib64/" || sudo cp -p "${SYSROOT}/lib/${LIB}" "${OUTDIR}/rootfs/lib/"
done


# TODO: Make device nodes
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1

# TODO: Clean and build the writer utility
cd ${FINDER_APP_DIR}
make clean
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
sudo cp -p ${FINDER_APP_DIR}/writer ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/finder.sh ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/finder-test.sh ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/conf/username.txt ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/conf/assignment.txt ${OUTDIR}/rootfs/home/
sudo cp -p ${FINDER_APP_DIR}/autorun-qemu.sh ${OUTDIR}/rootfs/home/


# TODO: Chown the root directory
sudo chown -R root:root ${OUTDIR}/rootfs
sudo chmod 4755 ${OUTDIR}/rootfs/bin/busybox

# TODO: Create initramfs.cpio.gz
cd ${OUTDIR}/rootfs
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/initramfs.cpio
gzip -f ${OUTDIR}/initramfs.cpio