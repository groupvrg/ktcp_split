#!/usr/bin/perl -w

use strict;
use threads;
use autodie;
use IO::Socket;

my $serverport = 5557;
my $server = IO::Socket::INET->new(LocalPort=>$serverport, Proto=>"tcp",
                                   Listen=>1024, Reuse=>1);

sub connection {
	my $sock = shift;
	my $bytes = 0;
	my $data;
	$sock->setsockopt(SOL_SOCKET, SO_RCVTIMEO, pack('l!l!',3,0));
	while (defined (my $rc = $sock->recv($data, 64 * 1024))) {
		$bytes += length ($data) if defined ($data);
		last unless (defined ($data) and (length $data) > 0);
	}
	printf "%s bye [$bytes]\n", $sock->peerport;
}

while (1) {
	threads->create(\&connection, $server->accept());
}

