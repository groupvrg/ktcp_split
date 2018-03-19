#!/usr/bin/perl -w

use strict;
use autodie;
use IO::Socket;
use Time::HiRes;

$| = 1; #?
sub parse_config {
	die "sorry: parse_config not implemented yet...\n";
}

sub get_params {
	if ( -r "./config" ) {
		return parse_config();
	}
}

my $serverip = '127.0.0.1';	#VMA
my $serverport = 9216;
my $size = (64 * 1024) - 28;
get_params();

my $time = Time::HiRes::gettimeofday();
my $message = IO::Socket::INET->new(Proto=>"udp", PeerPort=>$serverport,
				     PeerAddr=>$serverip);
$time = Time::HiRes::gettimeofday() - $time;
printf "connected...%f\n", $time;
$time = Time::HiRes::gettimeofday();
$message->send("Ping!");
print "Ping sent...\n";
my $data = "";
$message->recv($data, $size);
$time = Time::HiRes::gettimeofday() - $time;
printf ">$data<\n%f\n", $time;
$message->close();
