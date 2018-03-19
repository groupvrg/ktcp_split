#!/usr/bin/perl -w

use strict;
use autodie;
use IO::Socket;

sub parse_config {
	die "sorry: parse_config not implemented yet...\n";
}

sub get_params {
	if ( -r "./config" ) {
		return parse_config();
	}
}

my $serverport = 5555;
my $size = (64 * 1024) - 28;
get_params();

my $server = IO::Socket::INET->new(LocalPort=>$serverport, Proto=>"udp");

my ($datagram, $flags);

while ($server->recv($datagram, $size, $flags)) {
	my $ipaddr = $server->peerhost;
	my $ipport = $server->peerport;
	print "received ", length($datagram), "bytes from $ipaddr:$ipport\n";
	$ipaddr = inet_aton($ipaddr);
	my $addr = sockaddr_in(scalar($ipport), $ipaddr);
	send($server, "pong", 0, $addr);
}

