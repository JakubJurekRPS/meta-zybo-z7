#!/bin/bash
INPUT=$@   # $@ means the list of args passes from command line
# P[$#]={$@}
# echo ${P[1]}
if [ $# -gt "0" ] && [ $1 != "--help" ] # $# means num of arguments from command window
then 
    echo "args: " 
    for i in $@
    do
        echo "  $i"
    done
else
    echo "Usage: loadZynqImages.sh [target-sd/target-remotefs] [dev] [zimg/dtb/rootfs-all]"
    exit 0
fi

if [ $1 == "target-sd" ]; then
    # echo "target: SD card"
    TARGET=SD
elif [ $1 == "target-remotefs" ]; then
    # echo "target: remote filesystem"
    TARGET=REMOTE_FS
else
    echo "incorrect target"
    exit 0
fi
shift #first argument is no longer needed
echo "Target: $TARGET"

if [ $TARGET == "SD" ]
then
    DEV=$1
    shift #second argument is no longer needed
    [ ${DEV} == "sda" ] && echo "Dev cannot be sda" && exit 0

    DEV_SUB_DEVS=$(lsblk /dev/${DEV} -o NAME -P) # DEV_SUB_DEVS will be zero in case of incorrect device
    [ ${#DEV_SUB_DEVS} == 0 ] && echo "Correct dev not found" && exit 0

    # echo $DEV_SUB_DEVS
    while [ ${#DEV_SUB_DEVS} != 0 ] 
    do
        TMP=${DEV_SUB_DEVS##*=}
        DEV_SUB_DEVS=${DEV_SUB_DEVS//NAME=$TMP/}
        TMP=${TMP//\"}
        
        if [ ${TMP} != ${DEV}  ] #check if TMP is a partition on DEV and that it's not DEV itself
        then
            MOUNTPOINT=$(lsblk /dev/${TMP} -o MOUNTPOINT)
            
            MOUNTPOINT=$(echo $MOUNTPOINT | sed s/MOUNTPOINT/''/ | sed s/' '/''/)
            if [ ${#MOUNTPOINT} != 0 ]
            then
                FSTYPE=$(lsblk /dev/${TMP} -o FSTYPE)
                FSTYPE=$(echo $FSTYPE | sed s/FSTYPE' '/''/)
                if [ $FSTYPE == "vfat" ]
                then
                    DIR_DTB_ZIMG=$MOUNTPOINT
                elif [ $FSTYPE == "ext4" ]
                then    
                    DIR_ROOTFS=$MOUNTPOINT
                else 
                    echo "FSTYPE error"
                fi
            fi
        fi
    done
elif [ $TARGET == "REMOTE_FS" ]
then
    DIR_DTB_ZIMG=/srv/tftp
    DIR_ROOTFS=/srv/nfs/zynq_remote_rootfs
fi

echo DIR_DTB_ZIMG: $DIR_DTB_ZIMG
echo DIR_ROOTFS: $DIR_ROOTFS

echo "Do you want to continue? [y/n]"
read ack
[ $ack != "y" ] && echo exit && exit 0

if ! [ ${#DIR_DTB_ZIMG} -gt "0" ] 
then
    echo "ERROR: DIR_DTB_ZIMG is zero"
    exit 0
fi
if ! [ ${#DIR_ROOTFS} -gt "0" ] 
then
    echo "ERROR: DIR_ROOTFS is zero"
    exit 0
fi

# ls $DIR_DTB_ZIMG
# ls $DIR_ROOTFS
if [ $TARGET == "SD" ]
then
    if ! [[ $DIR_DTB_ZIMG =~ /media/* ]] 
    then 
        echo "Mountpoint is not in /media/\*"
        exit 0
    fi
    if ! [[ $DIR_ROOTFS =~ /media/* ]]
    then 
        echo "Mountpoint is not in /media/\*"
        exit 0
    fi
fi

[[ ${BBPATH} == 0 ]] && echo "BBPATH not set" && exit 0 || echo "BBPATH: $BBPATH"

[ $# -gt "0" ] || echo "Nothing to copy" || exit 0 
while [ $# -gt "0" ]
do
    case $1 in
        zimg)
            rm -i $DIR_DTB_ZIMG/zImage
            cp $BBPATH/tmp/deploy/images/zynq7000/zImage $DIR_DTB_ZIMG
            # mv -i $DIR_DTB_ZIMG/zImage--*.bin $DIR_DTB_ZIMG/zImage.bin
            ;;
        dtb)
            rm -i $DIR_DTB_ZIMG/*.dtb
            cp -i $BBPATH/tmp/deploy/images/zynq7000/system-top.dtb $DIR_DTB_ZIMG
            ;;
        rootfs-all)
            sudo find $DIR_ROOTFS -mindepth 1 -maxdepth 1 -type d -not -name 'opt' -exec rm -r {} \;
            sudo cp -i -f $BBPATH/tmp/deploy/images/zynq7000/*zynq7000-*.rootfs.tar.gz  $DIR_ROOTFS
            sudo gzip -d $DIR_ROOTFS/*zynq7000-*.rootfs.tar.gz
            sudo tar -xf $DIR_ROOTFS/*zynq7000-*.rootfs.tar -C$DIR_ROOTFS
            sudo rm $DIR_ROOTFS/*tar
            ;;
        *)
            echo "Error argument: \"$1\""
    esac
    shift
done