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

#my $serverport = 5556;
my $serverport = 9216;
my $size = (64 * 1024) - 28;
get_params();

my $server = IO::Socket::INET->new(LocalPort=>$serverport, Proto=>"tcp",
				   Listen=>5, Reuse=>1);

my ($datagram, $flags);

#while ($server->recv($datagram, $size, $flags)) {
#	my $ipaddr = $server->peerhost;
#	print "received ", length($datagram), "bytes from $ipaddr\n";
#}
while (1) {
	my $client = $server->accept();

	#get information about a newly connected client

	my $client_address = $client->peerhost();
	my $client_port = $client->peerport();
	print "connection from $client_address:$client_port\n";

	#read up to 1024 characters from the connected client
	my $data = "";
	$client->recv($data, $size);
	print "received data: $data\n";

	# write response data to the connected client
	$data = "ok";
	$client->send($data);

	# notify client that response has been sent
	shutdown($client, 1);
}
$server->close();
