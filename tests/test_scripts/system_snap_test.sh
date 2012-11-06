#!/bin/bash

TEST_DIR=$4
export XTREEMFS=$1
echo "XTREEMFS=$XTREEMFS"

if [[ "$2" == pbrpcs://* || "$2" == pbrpcg://* ]]; then
  CREDS="-c $1/tests/certs/Client.p12 -cpass passphrase -t $1/tests/certs/trusted.jks -tpass passphrase"
fi

#
# enable and create a snapshot
#

# enable snapshots
COMMAND="$1/bin/xtfsutil --enable-snapshots .."
echo "Running ${COMMAND}..."
$COMMAND
RESULT=$?
if [ "$RESULT" -ne "0" ]; then echo "$COMMAND failed"; exit $RESULT; fi

# create an empty directory
mkdir ./newdir

# take a snapshot
COMMAND="$1/bin/xtfsutil --create-snapshot mySnap ."
echo "Running ${COMMAND}..."
$COMMAND
RESULT=$?
if [ "$RESULT" -ne "0" ]; then echo "$COMMAND failed"; exit $RESULT; fi

# list all snapshots
COMMAND="$1/bin/xtfsutil --list-snapshots ."
echo "Running ${COMMAND}..."
$COMMAND |grep mySnap
RESULT=$?
if [ "$RESULT" -ne "0" ]; then echo "$COMMAND failed"; exit $RESULT; fi

# create file
echo "test" > file.txt

# TODO: mount and check the snapshot

# delete a snapshot
COMMAND="$1/bin/xtfsutil --delete-snapshot mySnap ."
echo "Running ${COMMAND}..."
$COMMAND
RESULT=$?
if [ "$RESULT" -ne "0" ]; then echo "$COMMAND failed"; exit $RESULT; fi

# TODO: mount and access a snapshot
