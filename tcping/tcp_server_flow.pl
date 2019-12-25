#!/usr/bin/perl -w

use strict;
use autodie;
use Time::HiRes;
use IO::Socket;

sub parse_config {
	die "sorry: parse_config not implemented yet...\n";
}

sub get_params {
	if ( -r "./config" ) {
		return parse_config();
	}
}

my $serverport = 8080;
#my $serverport = 9216;
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
	my $count = 0 ;
	my $start_time = Time::HiRes::gettimeofday();
	my $start = $start_time;
	do { 
		my $addr = $client->recv($data, $size);
		die "bye..." unless defined ($addr);
		my $time = Time::HiRes::gettimeofday() ;
		$count += $size;
		if (($time - $start_time)> 1 ) {
			$start_time = $time;
			printf "Received (%.2f): %.2fGb/s %d\n", ($time - $start), $count/(1024*1024*128), length($data);
			$count = 0;
		}
	} while (length($data) > 0);


	# notify client that response has been sent
	printf("Bye...\n");
	shutdown($client, 1);
}
$server->close();
