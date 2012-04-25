#
# Copyright (C) Imagination Technologies Ltd. All rights reserved.
# 
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
# 
# This program is distributed in the hope it will be useful but, except 
# as otherwise stated in writing, without any warranty; without even the 
# implied warranty of merchantability or fitness for a particular purpose. 
# See the GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License along with
# this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
# 
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
# Contact Information:
# Imagination Technologies Ltd. <gpl-support@imgtec.com>
# Home Park Estate, Kings Langley, Herts, WD4 8LZ, UK 
# 

usage() {
	echo "usage: $0 [--64] --cc CC --out OUT [cflag]"
	exit 1
}

do_cc() {
	echo "int main(void){return 0;}" | $CC $1 -xc -c - -o $ccof 2>/dev/null
}

while [ 1 ]; do
	if [ "$1" = "--64" ]; then
		BIT_CHECK=1
	elif [ "$1" = "--cc" ]; then
		[ "x$2" = "x" ] && usage
		CC="$2" && shift
	elif [ "$1" = "--out" ]; then
		[ "x$2" = "x" ] && usage
		OUT="$2" && shift
	elif [ "${1#--}" != "$1" ]; then
		usage
	else
		break
	fi
	shift
done

[ "x$CC" = "x" ] && usage
[ "x$OUT" = "x" ] && usage
ccof=$OUT/cc-sanity-check

if [ "x$BIT_CHECK" = "x1" ]; then
	do_cc ""
	file $ccof | grep -q 64-bit
	[ "$?" = "0" ] && echo true || echo false
else
	[ "x$1" = "x" ] && usage
	do_cc $1
	[ "$?" = "0" ] && echo $1
fi

rm -f $ccof
exit 0
