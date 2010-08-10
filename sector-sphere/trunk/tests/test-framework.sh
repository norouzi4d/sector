#!/bin/bash
# vim:expandtab:shiftwidth=4:softtabstop=4:tabstop=4:

absolute_path() {
    (cd `dirname $1`; echo $PWD/`basename $1`)
}

init_test_env() {
        export SECTOR=`absolute_path $SECTOR`
        export SECTOR_HOME=`absolute_path $SECTOR`
        export TESTSUITE=`basename $0 .sh`
        export START_MASTER=${START_MASTER:-"${SECTOR}/master/start_master"}
        export START_SECURITY=${START_SECURITY:-"${SECTOR}/security/sserver"}
        export START_SLAVE=${START_SLAVE:-"${SECTOR}/slave/start_slave"}
        export MASTER_CONF=${MASTER_CONF:-"${SECTOR}/conf/master.conf"}
        export SAVE_PWD=${SAVE_PWD:-${SECTOR}/tests}
        export COPY=${COPY:-${SECTOR}/tools/sector_cp}
        export MKDIR=${MKDIR:-${SECTOR}/tools/sector_mkdir}
        export LS=${LS:-${SECTOR}/tools/sector_ls}
        export MV=${MV:-${SECTOR}/tools/sector_mv}
        export RM=${RM:-${SECTOR}/tools/sector_rm}
        export UPLOAD=${UPLOAD:-${SECTOR}/tools/sector_upload}
        export DOWNLOAD=${DOWNLOAD:-${SECTOR}/tools/sector_download}
        export SYSINFO=${SYSINFO:-${SECTOR}/tools/sector_sysinfo}
        export DSH=${DSH:-"ssh"}
        export SECTOR_HOST=${SECTOR_HOST:-`hostname`}
        export TEST_FAILED=false

        if ! echo $PATH | grep -q $SECTOR/tests; then
            export PATH=$PATH:$SECTOR/tests
        fi

        if ! echo $PATH | grep -q $SECTOR/lib; then
            export PATH=$PATH:$SECTOR/lib
        fi

        if ! echo $LD_LIBRARY_PATH | grep -q $SECTOR/lib; then
            export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$SECTOR/lib
        fi

        ONLY=${ONLY:-$*}
        echo $PATH
}

build_test_filter() {
    [ "$ONLY" ] && echo "only running test `echo $ONLY`"
    for O in $ONLY; do
        eval ONLY_${O}=true 
    done
}

error_no_exit() {
        echo $@
        TEST_FAILED=true
}

error() {
        error_no_exit
        exit 1
}

basetest() {
    if [[ $1 = [a-z]* ]]; then
        echo $1
    else
        echo ${1%%[a-z]*}
    fi
}

pass() {
    $TEST_FAILED && echo -n "FAIL " || echo -n "PASS "
    echo $@
}

run_one() {
        local testnum=$1
        local message=$2
        tfile=f${testnum}
        export tdir=d${base}
        export TESTNAME=test_$testnum

        local BEFORE=`date +%s`
        echo "== test $testnum: $message == `date +%H:%M:%S` ($BEFORE)"
        TEST_FAILED=false
        test_${testnum} || echo "test_$testnum failed with $?"

        pass "($((`date +%s` - $BEFORE))s)"
        TEST_FAILED=false
        cd $SAVED_PWD 
        unset TESTNAME
        unset tdir
        unset tfile
        unset base
        return $?
}

run_test() {
    export base=`basetest $1`
    if [ ! -z "$ONLY" ]; then
        testname=ONLY_$1
        if [ ${!testname}x != x ]; then
            [ "$LAST_SKIPPED" ] && echo "" && LAST_SKIPPED=
            run_one $1 "$2"
            return $?
        fi
        testname=ONLY_$base
        if [ ${!testname}x != x ]; then
            [ "$LAST_SKIPPED" ] && echo "" && LAST_SKIPPED=
            run_one $1 "$2"
            return $?
        fi
        LAST_SKIPPED="y"
        echo -n "."
        return 0
    fi

    LAST_SKIPPED=
    run_one $1 "$2"

    return $?
}


