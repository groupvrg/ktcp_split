#!/usr/bin/perl -w
#
use strict;
use threads;
use autodie;
use IO::Socket;
use Time::HiRes;
#use Regexp::Common qw/ net number /;
$| = 1; #?

my $serverport = 5557;

my $buffer = 'a'x(1<<16);
my $len = length ($buffer);

sub bw {
        my $bytes = shift;
        my $time = shift;
        $bytes /= (128 * 1000); #(bytes * 8 / GB)
        return $bytes/$time;
}

sub thread {
        my $name = shift;
        my $r1 = int(rand(255));
        my $r2 = int(rand(255));
        my $serverip = "11.$r1.$r2.$name";

        my $conn = Time::HiRes::gettimeofday();
        my $message = IO::Socket::INET->new(Proto=>"tcp", PeerPort=>$serverport,
                                     PeerAddr=>$serverip);
        my $conn2 =  Time::HiRes::gettimeofday();
        $conn = $conn2 - $conn;

        my $runtime = Time::HiRes::gettimeofday();
        my $stop = $runtime;
        my $bytes = 0;
        while (($stop - $runtime) < 10) {
                $message->send($buffer);
                $bytes += $len;
                $stop = Time::HiRes::gettimeofday();
        }
        #$message->close();
        $message->shutdown(2);
        $stop = $stop - $runtime;
        $bytes =  bw($bytes, $stop);
        printf "[$name] %s->$serverip: connection time %f [%.3f, %.3f]\n", $message->sockport, \
                                $conn, $stop , $bytes;

        return $bytes;
}

for (my $iter = 0; $iter < 60; $iter++)  {
        for (my $i = 0; $i < 64; $i++) {
                threads->create(\&thread, $i);
        }
                sleep 4;
}

my $str = "NaN";
my $rc;

foreach my $thr(threads->list()) {
        $rc = $thr->join();
}

