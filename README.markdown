
Multicast Image Client/Server
=============================

A client to serve a large binary file (typically a disk image) to be transported to multiple listening clients. Allows clients to join at any point.

Client Invocation
-----------------

The client is invoked like this:

	multiclient [ -g GROUP ] [ -p PORT ] [ -t TIMEOUT ] [ -f FROM_PORT ] [ -s SERVER_PORT ] [ -f FROM_PORT ] [ -o OUTPUTNAME ]

GROUP is the multicast address, it defaults to `225.0.20.10`

PORT is the listen port, it defaults to `16000`

TIMEOUT means if the multicast channel goes quiet for this many milliseconds, the client will create and send a block request to the server. Defaults to `1000` or one second.

SERVER_PORT specifies the server's block map port, defaults to `7500`

FROM_PORT is a debug option, it sets the source port for the block map to a specific value. It defaults to a random number between 32767 and 65535.

OUTPUTNAME will override the output name supplied by the server. This allows an image file to be directly written to a disk by using `-o /dev/sda`

Server Invocation
-----------------

Invoke like this:

	multiserver -f FILENAME [ -g GROUP ] [ -p PORT ] [ -s SERVER_PORT ] [ -d DELAY ]

GROUP is the multicast address, it defaults to `225.0.20.10`

PORT is the listen port, it defaults to `16000`

SERVER_PORT specifies the server's block map port, defaults to `7500`

DELAY is the interpacket delay for the multicast channel. Don't use this, it makes things horrifically slow, and the block search algorithm means this isn't needed.

License
-------

Freely available under the terms of the GNU General Public License version 2.

Bugs
----

Could be. Send a bug report, or better yet a patch. Also, the code isn't well commented.
