#!/bin/sh
# 1.5.2006, Sampo Kellomaki (sampo@iki.fi)
# $Id: setup1.sh,v 1.2 2006/05/15 10:10:33 sampo Exp $
./s5066d -di sis-B -d -d -p sis:0.0.0.0:5065 -p dts:0.0.0.0:5067 &
./s5066d -di A-sis -d -d -p sis:0.0.0.0:5066 dts:localhost:5067 &
./s5066d -di Asmtp -d -d -p smtp:0.0.0.0:2525 sis:localhost:5066 &
#./s5066d -di smtpB -d -d sis:localhost:5065 smtp:ilpo:25 &
#./s5066d -di smtpB -d -d sis:localhost:5065 smtp:muffin.cellmail.com:25 &
./s5066d -di smtpB -d -d sis:localhost:5065 smtp:smtp.qindel.com:25 &
#EOF