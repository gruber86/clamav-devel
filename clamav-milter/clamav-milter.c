/*
 * clamav-milter.c
 *	.../clamav-milter/clamav-milter.c
 *
 *  Copyright (C) 2003 Nigel Horne <njh@bandsman.co.uk>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Install into /usr/local/sbin/clamav-milter, mode 744
 *
 * See http://www.nmt.edu/~wcolburn/sendmail-8.12.5/libmilter/docs/sample.html
 *
 * Installations for RedHat Linux and it's derivatives such as YellowDog:
 * 1) Ensure that you have the sendmail-devel RPM installed
 * 2) Add to /etc/mail/sendmail.mc:
 *	INPUT_MAIL_FILTER(`clamav', `S=local:/var/run/clamav.sock, F=, T=S:4m;R:4m')dnl
 *	define(`confINPUT_MAIL_FILTERS', `clamav')
 * 3) Check entry in /usr/local/etc/clamav.conf of the form:
 *	LocalSocket /var/run/clamd.sock
 *	StreamSaveToDisk
 * 4) If you already have a filter (such as spamassassin-milter from
 * http://savannah.nongnu.org/projects/spamass-milt) add it thus:
 *	INPUT_MAIL_FILTER(`clamav', `S=local:/var/run/clamav.sock, F=, T=S:4m;R:4m')dnl
 *	INPUT_MAIL_FILTER(`spamassassin', `S=local:/var/run/spamass.sock, F=, T=C:15m;S:4m;R:4m;E:10m')
 *	define(`confINPUT_MAIL_FILTERS', `spamassassin,clamav')dnl
 * 5) You may find INPUT_MAIL_FILTERS is not needed on your machine, however it
 * is recommended by the Sendmail documentation and I suggest going along
 * with that.
 * 6) I suggest putting SpamAssassin first since you're more likely to get spam
 * than a virus/worm sent to you.
 * 7) Add to /etc/sysconfig/clamav-milter
 *	CLAMAV_FLAGS="--max-children=2 local:/var/run/clamav.sock"
 * or if clamd is on a different machine
 *	CLAMAV_FLAGS="--max-children=2 --server=192.168.1.9 local:/var/run/clamav.sock"
 * 8) You should have received a script to put into /etc/init.d with this
 * software.
 *
 * Tested OK on Linux/x86 (RH8.0) with gcc3.2.
 *	cc -O3 -pedantic -Wuninitialized -Wall -pipe -mcpu=pentium -march=pentium -fomit-frame-pointer -ffast-math -finline-functions -funroll-loops clamav-milter.c -pthread -lmilter ../libclamav/.libs/libclamav.a ../clamd/cfgfile.o ../clamd/others.o
 * Compiles OK on Linux/x86 with tcc 0.9.16, but fails to link errors with 'atexit'
 *	tcc -g -b -lmilter -lpthread clamav-milter.c...
 * Fails to compile on Linux/x86 with icc6.0 (complains about stdio.h...)
 *	icc -O3 -tpp7 -xiMKW -ipo -parallel -i_dynamic -w2 clamav-milter.c...
 * Fails to build on Linux/x86 with icc7.1 with -ipo (fails on libclamav.a - keeps saying run ranlib). Otherwise it builds and runs OK.
 *	icc -O2 -tpp7 -xiMKW -parallel -i_dynamic -w2 -march=pentium4 -mcpu=pentium4 clamav-milter.c...
 * Tested with Electric Fence 2.2.2
 *
 * Compiles OK on Linux/ppc (YDL2.3) with gcc2.95.4. Needs -lsmutil to link.
 *	cc -O3 -pedantic -Wuninitialized -Wall -pipe -fomit-frame-pointer -ffast-math -finline-functions -funroll-loop -pthread -lmilter ../libclamav/.libs/libclamav.a ../clamd/cfgfile.o ../clamd/others.o -lsmutil
 * I haven't tested it further on this platform yet.
 * YDL3.0 should compile out of the box
	cc -O3 -pedantic -Wuninitialized -Wall -pipe -fomit-frame-pointer -ffast-math -finline-functions -funroll-loop -pthread -lmilter ../libclamav/.libs/libclamav.a ../clamd/cfgfile.o ../clamd/others.o -lsmutil
 *
 * Sendmail on MacOS/X (10.1) is provided without a development package so this
 * can't be run "out of the box"
 *
 * Solaris 8 doesn't have milter support so clamav-milter won't work unless
 * you rebuild sendmail from source.
 * Solaris 9 has milter support in the supplied sendmail, but doesn't include
 * libmilter so you can't develop milter applications on it. Go to sendmail.org,
 * download the lastest sendmail, cd to libmilter and "make install" there.
 * Needs -lresolv
 *
 * FreeBSD4.7 use /usr/local/bin/gcc30. GCC3.0 is an optional extra on
 * FreeBSD. It comes with getopt.h which is handy. To link you need
 * -lgnugetopt
 *	gcc30 -O3 -DCONFDIR=\"/usr/local/etc\" -I. -I.. -I../clamd -I../libclamav -pedantic -Wuninitialized -Wall -pipe -mcpu=pentium -march=pentium -fomit-frame-pointer -ffast-math -finline-functions -funroll-loops clamav-milter.c -pthread -lmilter ../libclamav/.libs/libclamav.a ../clamd/cfgfile.o ../clamd/others.o -lgnugetopt
 *
 * FreeBSD4.8: should compile out of the box
 * OpenBSD3.3: the supplied sendmail does not come with Milter support. You
 * will need to rebuild sendmail from source
 *
 * Changes
 *	0.2:	4/3/03	clamfi_abort() now always calls pthread_mutex_unlock
 *		5/3/03	Only send a bounce if -b is set
 *			Version now uses -v not -V
 *			--config-file couldn't be set by -c
 *	0.3	7/3/03	Enhanced the Solaris compile time comment
 *			No need to save the return result of LogSyslog
 *			Use LogVerbose
 *	0.4	9/3/03	Initialise dataSocket/cmdSocket correctly
 *		10/3/03	Say why we don't connect() to clamd
 *			Enhanced '-l' usage message
 *	0.5	18/3/03	Ported to FreeBSD 4.7
 *			Source no longer in support, so remove one .. from
 *			the build instructions
 *			Corrected the use of strerror_r
 *	0.51	20/3/03	Mention StreamSaveToDisk in the installation
 *			Added -s option which allows clamd to run on a
 *			different machine from the milter
 *	0.52	20/3/03	-b flag now only stops the bounce, sends warning
 *			to recipient and postmaster
 *	0.53	24/3/03	%d->%u in syslog call
 *		27/3/03	tcpSocket is now of type in_port_t
 *		27/3/03	Use PING/PONG
 *	0.54	23/5/03	Allow a range of IP addresses as outgoing ones
 *			that need not be checked
 *	0.55	24/5/03	Use inet_ntop() instead of inet_ntoa()
 *			Thanks to Krzysztof Olędzki <ole@ans.pl>
 *	0.60	11/7/03	Added suggestions by Nigel Kukard <nkukard@lbsd.net>
 *			Should stop a couple of remote chances of crashes
 *	0.60a	22/7/03	Tidied up message when sender is unknown
 *	0.60b	17/8/03	Optionally set postmaster address. Usually one uses
 *			/etc/aliases, but not everyone want's to...
 *	0.60c	22/8/03	Another go at Solaris support
 *	0.60d	26/8/03	Removed superflous buffer and unneeded strerror call
 *			ETIMEDOUT isn't an error, but should give a warning
 *	0.60e	09/9/03	Added -P and -q flags by "Nicholas M. Kirsch"
 *			<nick@kirsch.org>
 */

#define	CM_VERSION	"0.60e"

/*#define	CONFDIR	"/usr/local/etc"*/

#include "defaults.h"
#include "cfgfile.h"
#include "../target.h"

#ifndef	CL_DEBUG
#define	NDEBUG
#endif

#include <stdio.h>
#include <sysexits.h>
#ifndef TARGET_OS_FREEBSD
#include <malloc.h>
#endif
#include <syslog.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <assert.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdarg.h>
#include <errno.h>
#include <libmilter/mfapi.h>
#include <pthread.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <signal.h>
#include <regex.h>	// njh@bandsman.co.uk

#define _GNU_SOURCE
#include "getopt.h"

/*
 * TODO: optional: xmessage on console when virus stopped (SNMP would be real nice!)
 * TODO: allow -s server to use a name as well as an IP address
 * TODO: build with libclamav.so rather than libclamav.a
 * TODO: check security - which UID will this run under?
 * TODO: bounce message should optionally be read from a file
 * TODO: optionally add a signature that the message has been scanned with ClamAV
 * TODO: Use MaxThreads from the conf file rather than max-children
 * TODO: Support ThreadTimeout, LogTime and Logfile from the conf
 *	 file
 * TODO: Allow more than one clamdscan server to be given
 * TODO: Optionally quanrantine infected e-mails
 */

/*
 * Each thread has one of these
 */
struct	privdata {
	char	*from;	/* Who sent the message */
	char	**to;	/* Who is the message going to */
	int	numTo;	/* Number of people the message is going to */
	int	cmdSocket;	/*
				 * Socket to send/get commands e.g. PORT for
				 * dataSocket
				 */
	int	dataSocket;	/* Socket to send data to clamd */
};

static	int	pingServer(void);
static	sfsistat	clamfi_connect(SMFICTX *ctx, char *hostname, _SOCK_ADDR *hostaddr);
static	sfsistat	clamfi_envfrom(SMFICTX *ctx, char **argv);
static	sfsistat	clamfi_envrcpt(SMFICTX *ctx, char **argv);
static	sfsistat	clamfi_header(SMFICTX *ctx, char *headerf, char *headerv);
static	sfsistat	clamfi_eoh(SMFICTX *ctx);
static	sfsistat	clamfi_body(SMFICTX *ctx, u_char *bodyp, size_t len);
static	sfsistat	clamfi_eom(SMFICTX *ctx);
static	sfsistat	clamfi_abort(SMFICTX *ctx);
static	sfsistat	clamfi_close(SMFICTX *ctx);
static	void		clamfi_cleanup(SMFICTX *ctx);
static	int		clamfi_send(const struct privdata *privdata, size_t len, const char *format, ...);
static	char		*strrcpy(char *dest, const char *source);

static	char	clamav_version[64];
static	int	oflag = 0;	/* scan messages from our machine? */
static	int	lflag = 0;	/* scan messages from our site? */
static	int	bflag = 0;	/*
				 * send a failure (bounce) message to the
				 * sender. This probably isn't a good idea
				 * since most reply addresses will be fake
				 */
static	int	pflag = 0;	/*
				 * Send a warning to the postmaster only,
				 * this means user's won't be told when someone
				 * sent them a virus
				 */
static	int	qflag = 0;	/*
				 * Send no warnings when a virus is found,
				 * this means that the only log of viruses
				 * found is the syslog, so it's best to
				 * enable LogSyslog in clamav.conf
				 */

#ifdef	CL_DEBUG
static	int	debug_level = 0;
#endif

static	pthread_mutex_t	n_children_mutex = PTHREAD_MUTEX_INITIALIZER;
static	pthread_cond_t	n_children_cond = PTHREAD_COND_INITIALIZER;
static	unsigned	int	n_children = 0;
static	unsigned	int	max_children = 0;
static	int	use_syslog = 0;
static	int	logVerbose = 0;
static	struct	cfgstruct	*copt;
static	const	char	*localSocket;
static	in_port_t	tcpSocket;
static	const	char	*serverIP = "127.0.0.1";
static	const	char	*postmaster = "postmaster";

static void
help(void)
{
	printf("\n\tclamav-milter version %s\n", CM_VERSION);
	puts("\tCopyright (C) 2003 Nigel Horne <njh@despammed.com>\n");

	puts("\t--bounce\t\t-b\tSend a failure message to the sender.");
	puts("\t--config-file=FILE\t-c FILE\tRead configuration from FILE.");
	puts("\t--help\t\t\t-h\tThis message.");
	puts("\t--local\t\t\t-l\tScan messages sent from machines on our LAN.");
	puts("\t--outgoing\t\t-o\tScan outgoing messages from this machine.");
	puts("\t--postmaster\t\t-p\tPostmaster address [default=postmaster].");
	puts("\t--postmaster-only\t\t-P\tSend warnings only to the postmaster.");
	puts("\t--quiet\t\t\t-q\tDon't send e-mail notifications of interceptions.");
	puts("\t--server=ADDRESS\t-s ADDRESS\tIP address of server running clamd (when using TCPsocket).");
	puts("\t--version\t\t-V\tPrint the version number of this software.");
#ifdef	CL_DEBUG
	puts("\t--debug-level=n\t\t-x n\tSets the debug level to 'n'.");
#endif
}

int
main(int argc, char **argv)
{
	extern char *optarg;
	char *port = NULL, *ptr;
	FILE *clamd;
	const char *cfgfile = CL_DEFAULT_CFG;
	struct cfgstruct *cpt;
	char cmd[64];
	struct smfiDesc smfilter = {
		"ClamAv", /* filter name */
		SMFI_VERSION,	/* version code -- leave untouched */
		SMFIF_ADDHDRS,	/* flags - we add headers */
		clamfi_connect, /* connection callback */
		NULL, /* HELO filter callback */
		clamfi_envfrom, /* envelope sender filter callback */
		clamfi_envrcpt, /* envelope recipient filter callback */
		clamfi_header, /* header filter callback */
		clamfi_eoh, /* end of header callback */
		clamfi_body, /* body filter callback */
		clamfi_eom, /* end of message callback */
		clamfi_abort, /* message aborted callback */
		clamfi_close, /* connection cleanup callback */
	};

	for(;;) {
		int opt_index = 0;
#ifdef	CL_DEBUG
		const char *args = "bc:lopPqdhs:Vx:";
#else
		const char *args = "bc:lopPqdhs:V";
#endif
		static struct option long_options[] = {
			{
				"bounce", 0, NULL, 'b'
			},
			{
				"config-file", 1, NULL, 'c'
			},
			{
				"help", 0, NULL, 'h'
			},
			{
				"local", 0, NULL, 'l'
			},
			{
				"outgoing", 0, NULL, 'o'
			},
			{
				"postmaster", 0, NULL, 'p'
			},
			{
				"postmaster-only", 0, NULL, 'P',
			},
			{
				"quiet", 0, NULL, 'q'
			},
			{
				"max-children", 1, NULL, 'm'
			},
			{
				"server", 1, NULL, 's'
			},
			{
				"version", 0, NULL, 'V'
			},
#ifdef	CL_DEBUG
			{
				"debug-level", 1, NULL, 'x'
			},
#endif
			{
				NULL, 0, NULL, '\0'
			}
		};

		int ret = getopt_long(argc, argv, args, long_options, &opt_index);

		if(ret == -1)
			break;
		else if(ret == 0)
			ret = long_options[opt_index].val;

		switch(ret) {
			case 'b':	/* bounce worms/viruses */
				bflag++;
				break;
			case 'c':	/* where is clamav.conf? */
				cfgfile = optarg;
				break;
			case 'h':
				help();
				return EX_OK;
			case 'l':	/* scan mail from the lan */
				lflag++;
				break;
			case 'm':	/* maximum number of children */
				max_children = atoi(optarg);
				break;
			case 'o':	/* scan outgoing mail */
				oflag++;
				break;
			case 'p':	/* postmaster e-mail address */
				postmaster = optarg;
				break;
			case 'P':	/* postmaster only */
				pflag++;
				break;
			case 'q':	/* send NO notification email */
				qflag++;
				break;
			case 's':	/* server running clamd */
				serverIP = optarg;
				break;
			case 'V':
				printf("%s version %s\n", argv[0], CM_VERSION);
				return EX_OK;
#ifdef	CL_DEBUG
			case 'x':
				debug_level = atoi(optarg);
				break;
#endif
			default:
#ifdef	CL_DEBUG
				fprintf(stderr, "Usage: %s [-b] [-c=FILE] [--max-children=num] [-l] [-o] [-p=address] [-P] [-q] [-x#] socket-addr\n", argv[0]);
#else
				fprintf(stderr, "Usage: %s [-b] [-c=FILE] [--max-children=num] [-l] [-o] [-p=address] [-P] [-q] socket-addr\n", argv[0]);
#endif
				return EX_USAGE;
		}
	}

	if (optind == argc) {
		fprintf(stderr, "%s: No socket-addr given\n", argv[0]);
		return EX_USAGE;
	}
	port = argv[optind];

	/*
	 * Sanity checks on the clamav configuration file
	 */
	if((copt = parsecfg(cfgfile)) == NULL) {
		fprintf(stderr, "%s: Can't parse the config file %s\n",
			argv[0], cfgfile);
		return EX_CONFIG;
	}

	if(!cfgopt(copt, "StreamSaveToDisk")) {
		fprintf(stderr, "%s: StreamSavetoDisk not enabled in %s\n",
			argv[0], cfgfile);
		return EX_CONFIG;
	}

	if(!cfgopt(copt, "ScanMail")) {
		fprintf(stderr, "%s: ScanMail not enabled in %s\n",
			argv[0], cfgfile);
		return EX_CONFIG;
	}

	/*
	 * Get the outgoing socket details - the way to talk to clamd
	 * TODO: support TCP sockets
	 */
	if((cpt = cfgopt(copt, "LocalSocket")) != NULL)
		/*
		 * TODO: check --server hasn't been set
		 */
		localSocket = cpt->strarg;
	else if((cpt = cfgopt(copt, "TCPSocket")) != NULL) {
		/*
		 * TCPSocket is in fact a port number not a full socket
		 */
		tcpSocket = (in_port_t)cpt->numarg;
		if(!pingServer()) {
			fprintf(stderr, "Can't talk to clamd server at %s on port %d\n",
				serverIP, tcpSocket);
			fprintf(stderr, "Check your entry for TCPSocket in %s\n",
				cfgfile);
			return EX_CONFIG;
		}
	} else {
		fprintf(stderr, "%s: You must select server type (local/TCP) in %s\n",
			argv[0], cfgfile);
		return EX_CONFIG;
	}
	if(localSocket && tcpSocket) {
		fprintf(stderr, "%s: You can select one server type only (local/TCP) in %s\n",
			argv[0], cfgfile);
		return EX_CONFIG;
	}

	/*
	 * call clamdscan to get the version number of clamd.
	 * TODO: there's probably a better way of doing this!
	 */
	snprintf(cmd, sizeof(cmd), "clamdscan --version 2>&1");
	clamd = popen(cmd, "r");

	if(clamd == NULL) {
		/*
		 * TODO: if this happens we should continue, allowing
		 * everything through with a warning
		 */
		fprintf(stderr, "%s: can't find clamdscan\n", argv[0]);
		return EX_TEMPFAIL;
	}

	fgets(clamav_version, sizeof(clamav_version), clamd);
	pclose(clamd);

	if((ptr = strchr(clamav_version, '\n')) != NULL)
		*ptr = '\0';

	if(!cfgopt(copt, "Foreground"))
		switch(fork()) {
			case -1:
				perror("fork");
				return EX_TEMPFAIL;
			case 0:	/* child */
				break;
			default:	/* parent */
				return EX_OK;
		}

	if(smfi_setconn(port) == MI_FAILURE) {
		fprintf(stderr, "%s: smfi_setconn failed\n",
			argv[0]);
		return EX_SOFTWARE;
	}

	if(cfgopt(copt, "LogSyslog")) {
		openlog("clamav-milter", LOG_CONS|LOG_PID, LOG_MAIL);
		syslog(LOG_INFO, clamav_version);
#ifdef	CL_DEBUG
		if(debug_level > 0)
			syslog(LOG_DEBUG, "Debugging is on");
#endif
		use_syslog = 1;

		if(cfgopt(copt, "LogVerbose"))
			logVerbose = 1;
	} else {
		if(qflag)
			fprintf(stderr, "%s: (-q && !LogSysLog): warning - all interception message methods are off\n",
				argv[0]);
		use_syslog = 0;
	}

	/*
	 * Get the incoming socket details - the way sendmail talks to us
	 *
	 * TODO: There's a security problem here that'll need fixing
	 */
	if(strncasecmp(port, "unix:", 5) == 0)
		unlink(port + 5);
	else if(strncasecmp(port, "local:", 6) == 0)
		unlink(port + 6);

	if(smfi_register(smfilter) == MI_FAILURE) {
		fprintf(stderr, "smfi_register failure\n");
		return EX_UNAVAILABLE;
	}

	signal(SIGPIPE, SIG_IGN);

	return smfi_main();
}

/*
 * Verify that the server is where we think it is
 * Returns true or false
 */
static int
pingServer(void)
{
	struct sockaddr_in server;
	int sock, nbytes;
	char buf[6];

	memset((char *)&server, 0, sizeof(struct sockaddr_in));
	server.sin_family = AF_INET;
	server.sin_port = htons(tcpSocket);
	server.sin_addr.s_addr = inet_addr(serverIP);

	if((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return 0;
	}
	if(connect(sock, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0) {
		perror("connect");
		return 0;
	}
	if(send(sock, "PING\n", 5, 0) < 5) {
		perror("send");
		close(sock);
		return 0;
	}

	shutdown(sock, SHUT_WR);

	nbytes = recv(sock, buf, sizeof(buf), 0);

	close(sock);

	if(nbytes < 0) {
		perror("recv");
		return 0;
	}
	buf[nbytes] = '\0';

	return strcmp(buf, "PONG\n") == 0;
}

static sfsistat
clamfi_connect(SMFICTX *ctx, char *hostname, _SOCK_ADDR *hostaddr)
{
	char buf[INET_ADDRSTRLEN];	/* IPv4 only */
	const char *remoteIP = inet_ntop(AF_INET, &((struct sockaddr_in *)(hostaddr))->sin_addr, buf, sizeof(buf));

#ifdef	CL_DEBUG
	assert(remoteIP != NULL);
#endif

	if(use_syslog)
		syslog(LOG_NOTICE, "clamfi_connect: connection from %s [%s]", hostname, remoteIP);
	printf("clamfi_connect: connection from %s [%s]\n", hostname, remoteIP);

	if(!oflag)
		if(strcmp(remoteIP, "127.0.0.1") == 0) {
#ifdef	CL_DEBUG
			if(use_syslog)
				syslog(LOG_DEBUG, "clamfi_connect: not scanning outgoing messages");
			puts("clamfi_connect: not scanning outgoing messages");
#endif
			return SMFIS_ACCEPT;
		}
	if(!lflag) {
		/*
		 * Decide what constitutes a local IP address. Emails from
		 * local machines are not scanned.
		 *
		 * TODO: read these from clamav.conf
		 */
		static const char *localAddresses[] = {
			/*"^192\\.168\\.[0-9]+\\.[0-9]+$",*/
			"^192\\.168\\.[0-9]*\\.[0-9]*$",
			"^10\\.0\\.0\\.[0-9]*$",
			"127.0.0.1",
			NULL
		};
		const char **possible;

		for(possible = localAddresses; *possible; possible++) {
			int rc;
			regex_t reg;

			if(regcomp(&reg, *possible, 0) != 0) {
				if(use_syslog)
					syslog(LOG_ERR, "Couldn't parse local regexp");
				return SMFIS_TEMPFAIL;
			}

			rc = (regexec(&reg, remoteIP, 0, NULL, 0) == REG_NOMATCH) ? 0 : 1;

			regfree(&reg);

			if(rc) {
#ifdef	CL_DEBUG
				if(use_syslog)
					syslog(LOG_DEBUG, "clamfi_connect: not scanning local messages");
				puts("clamfi_connect: not scanning outgoing messages");
#endif
				return SMFIS_ACCEPT;
			}
		}
	}

	return SMFIS_CONTINUE;
}

static sfsistat
clamfi_envfrom(SMFICTX *ctx, char **argv)
{
	struct privdata *privdata;
	struct sockaddr_in reply;
	short port;
	int nbytes, rc;
	char buf[64];

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_envfrom: %s", argv[0]);

#ifdef	CL_DEBUG
	printf("clamfi_envfrom: %s\n", argv[0]);
#endif

	if(max_children > 0) {
		rc = 0;

		pthread_mutex_lock(&n_children_mutex);

		while((n_children >= max_children) && (rc != ETIMEDOUT)) {
			struct timeval now;
			struct timespec timeout;
			struct timezone tz;

			/*
			 * Use pthread_cond_timedwait rather than
			 * pthread_cond_wait since the sendmail which calls
			 * us will have a timeout that we don't want to exceed
			 *
			 * Wait for a maximum of 1 minute.
			 *
			 * TODO: this timeout should be configurable
			 * It stops sendmail getting fidgety.
			 */
			gettimeofday(&now, &tz);
			timeout.tv_sec = now.tv_sec + 60;
			timeout.tv_nsec = 0;

			if(use_syslog)
				syslog(LOG_NOTICE,
					"hit max-children limit (%u >= %u): waiting for some to exit",
					n_children, max_children);
			rc = pthread_cond_timedwait(&n_children_cond, &n_children_mutex, &timeout);
#ifdef	CL_DEBUG
			if(rc != 0) {
#else
			if((rc != 0) && use_syslog) {
#endif
				char message[64];

#ifdef TARGET_OS_SOLARIS        /* no strerror_r */
				snprintf(message, sizeof(message), "pthread_cond_timedwait: %s", strerror(rc));
#else
				strerror_r(rc, buf, sizeof(buf));
				snprintf(message, sizeof(message), "pthread_cond_timedwait: %s", buf);
#endif
				if(use_syslog) {
					if(rc == ETIMEDOUT)
						syslog(LOG_NOTICE, message);
					else
						syslog(LOG_ERR, message);
				}
#ifdef	CL_DEBUG
				puts(message);
#endif
			}
		}
		n_children++;

#ifdef	CL_DEBUG
		printf(">n_children = %d\n", n_children);
#endif
		pthread_mutex_unlock(&n_children_mutex);

		if(rc == ETIMEDOUT) {
#ifdef	CL_DEBUG
			if(use_syslog)
				syslog(LOG_NOTICE, "Timeout waiting for a child to die");
			puts("Timeout waiting for a child to die");
#endif
		}
	}

	privdata = (struct privdata *)calloc(1, sizeof(struct privdata));
	privdata->dataSocket = -1;	/* 0.4 */
	privdata->cmdSocket = -1;	/* 0.4 */

	/*
	 * Create socket to talk to clamd. It will tell us the port to use
	 * to send the data. That will require another socket.
	 */
	if(localSocket) {
		struct sockaddr_un server;

		memset((char *)&server, 0, sizeof(struct sockaddr_un));
		server.sun_family = AF_UNIX;
		strncpy(server.sun_path, localSocket, sizeof(server.sun_path));

		if((privdata->cmdSocket = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
			perror("socket");
			return EX_TEMPFAIL;
		}
		if(connect(privdata->cmdSocket, (struct sockaddr *)&server, sizeof(struct sockaddr_un)) < 0) {
			perror(localSocket);
			return EX_TEMPFAIL;
		}
	} else {
		struct sockaddr_in server;

		memset((char *)&server, 0, sizeof(struct sockaddr_in));
		server.sin_family = AF_INET;
		server.sin_port = htons(tcpSocket);
		server.sin_addr.s_addr = inet_addr(serverIP);

		if((privdata->cmdSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			perror("socket");
			return EX_TEMPFAIL;
		}
		if(connect(privdata->cmdSocket, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0) {
			perror("connect");
			return EX_TEMPFAIL;
		}
	}

	/*
	 * Create socket that we'll use to send the data to clamd
	 */
	if((privdata->dataSocket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		close(privdata->cmdSocket);
		free(privdata);
		if(use_syslog)
			syslog(LOG_ERR, "send failed to create socket");
		return SMFIS_TEMPFAIL;
	}

	shutdown(privdata->dataSocket, SHUT_RD);

	if(send(privdata->cmdSocket, "STREAM\n", 7, 0) < 7) {
		perror("send");
		close(privdata->dataSocket);
		close(privdata->cmdSocket);
		free(privdata);
		if(use_syslog)
			syslog(LOG_ERR, "send failed to clamd");
		return SMFIS_TEMPFAIL;
	}

	shutdown(privdata->cmdSocket, SHUT_WR);

	nbytes = recv(privdata->cmdSocket, buf, sizeof(buf), 0);
	if(nbytes < 0) {
		perror("recv");
		close(privdata->dataSocket);
		close(privdata->cmdSocket);
		free(privdata);
		if(use_syslog)
			syslog(LOG_ERR, "recv failed from clamd getting PORT");
		return SMFIS_TEMPFAIL;
	}
	buf[nbytes] = '\0';
#ifdef	CL_DEBUG
	if(debug_level >= 4)
		printf("Received: %s", buf);
#endif
	if(sscanf(buf, "PORT %hd\n", &port) != 1) {
		close(privdata->dataSocket);
		close(privdata->cmdSocket);
		free(privdata);
		fprintf(stderr, "Expected port information from clamd, got '%s'\n",
			buf);
		if(use_syslog)
			syslog(LOG_ERR, "Expected port information from clamd, got '%s'",
				buf);
		return SMFIS_TEMPFAIL;
	}

	memset((char *)&reply, 0, sizeof(struct sockaddr_in));
	reply.sin_family = AF_INET;
	reply.sin_port = ntohs(port);

	reply.sin_addr.s_addr = inet_addr(serverIP);

#ifdef	CL_DEBUG
	if(debug_level >= 4)
		printf("Connecting to local port %d\n", port);
#endif

	rc = connect(privdata->dataSocket, (struct sockaddr *)&reply, sizeof(struct sockaddr_in));

	if(rc < 0) {
		perror("connect");

		close(privdata->dataSocket);
		close(privdata->cmdSocket);
		free(privdata);

		/* 0.4 - use better error message */
		if(use_syslog) {
#ifdef TARGET_OS_SOLARIS        /* no strerror_r */
			syslog(LOG_ERR, "Failed to connect to port %d given by clamd: %s", port, strerror(rc));
#else
			strerror_r(rc, buf, sizeof(buf));
			syslog(LOG_ERR, "Failed to connect to port %d given by clamd: %s", port, buf);
#endif
		}

		return SMFIS_TEMPFAIL;
	}

	clamfi_send(privdata, 0, "From %s\n", argv[0]);
	clamfi_send(privdata, 0, "From: %s\n", argv[0]);

	privdata->from = strdup(argv[0]);
	privdata->to = NULL;

	return (smfi_setpriv(ctx, privdata) == MI_SUCCESS) ? SMFIS_CONTINUE : SMFIS_TEMPFAIL;
}

static sfsistat
clamfi_envrcpt(SMFICTX *ctx, char **argv)
{
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_envrcpt: %s", argv[0]);

#ifdef	CL_DEBUG
	printf("clamfi_envrcpt: %s \n", argv[0]);
#endif

	clamfi_send(privdata, 0, "To: %s\n", argv[0]);

	if(privdata->to == NULL) {
		privdata->to = malloc(sizeof(char *) * 2);

		assert(privdata->numTo == 0);
	} else
		privdata->to = realloc(privdata->to, sizeof(char *) * (privdata->numTo + 2));

	privdata->to[privdata->numTo] = strdup(argv[0]);
	privdata->to[++privdata->numTo] = NULL;

	return SMFIS_CONTINUE;
}

static sfsistat
clamfi_header(SMFICTX *ctx, char *headerf, char *headerv)
{
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_header: %s: %s", headerf, headerv);
#ifdef	CL_DEBUG
	if(debug_level >= 9)
		printf("clamfi_header: %s: %s\n", headerf, headerv);
	else
		puts("clamfi_header");
#endif

	if(clamfi_send(privdata, 0, "%s: %s\n", headerf, headerv) < 0) {
		clamfi_cleanup(ctx);
		return SMFIS_TEMPFAIL;
	}
	return SMFIS_CONTINUE;
}

static sfsistat
clamfi_eoh(SMFICTX *ctx)
{
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_eoh");
#ifdef	CL_DEBUG
	puts("clamfi_eoh");
#endif

	if(clamfi_send(privdata, 1, "\n") < 0) {
		clamfi_cleanup(ctx);
		return SMFIS_TEMPFAIL;
	}

	return SMFIS_CONTINUE;
}

static sfsistat
clamfi_body(SMFICTX *ctx, u_char *bodyp, size_t len)
{
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_envbody: %u bytes", len);
#ifdef	CL_DEBUG
	printf("clamfi_envbody: %u bytes\n", len);
#endif

	if(clamfi_send(privdata, len, (char *)bodyp) < 0) {
		clamfi_cleanup(ctx);
		return SMFIS_TEMPFAIL;
	}
	return SMFIS_CONTINUE;
}

static sfsistat
clamfi_eom(SMFICTX *ctx)
{
	int rc = SMFIS_CONTINUE;
	char *ptr;
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);
	char mess[128];

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_eom");
#ifdef	CL_DEBUG
	puts("clamfi_eom");
	assert(privdata != NULL);
	assert(privdata->cmdSocket >= 0);
	assert(privdata->dataSocket >= 0);
#endif

	close(privdata->dataSocket);
	privdata->dataSocket = -1;

	if(recv(privdata->cmdSocket, mess, sizeof(mess), 0) > 0) {
		if((ptr = strchr(mess, '\n')) != NULL)
			*ptr = '\0';

		if(logVerbose)
			syslog(LOG_DEBUG, "clamfi_eom: read %s", mess);
#ifdef	CL_DEBUG
		printf("clamfi_eom: read %s\n", mess);
#endif
	} else {
		syslog(LOG_NOTICE, "clamfi_eom: read nothing from clamd");
#ifdef	CL_DEBUG
		puts("clamfi_eom: read nothing from clamd");
#endif
		mess[0] = '\0';
	}

	if(strstr(mess, "FOUND") == NULL) {
		smfi_addheader(ctx, "X-Virus-Scanned", clamav_version);

		/*
		 * TODO: if privdata->from is NULL it's probably SPAM, and
		 * me might consider bouncing it...
		 */
		if(use_syslog)
			syslog(LOG_NOTICE, "clean message from %s",
				(privdata->from) ? privdata->from : "an unknown sender");
	} else {
		int i;
		char **to, *err;
		FILE *sendmail;

		if(use_syslog)
			syslog(LOG_NOTICE, mess);

		/*
		 * Setup err as a list of recipients
		 */
		err = (char *)malloc(1024);

		sprintf(err, "Intercepted virus from %s to", privdata->from);

		ptr = strchr(err, '\0');

		i = 1024;

		for(to = privdata->to; *to; to++) {
			/*
			 * Re-alloc if we are about run out of buffer space
			 */
			if(&ptr[strlen(*to) + 2] >= &err[i]) {
				i += 1024;
				err = realloc(err, i);
			}
			ptr = strrcpy(ptr, " ");
			ptr = strrcpy(ptr, *to);
		}
		(void)strcpy(ptr, "\n");

		if(use_syslog)
			syslog(LOG_NOTICE, err);
#ifdef	CL_DEBUG
		puts(err);
#endif

		if(!qflag) {
			sendmail = popen("/usr/lib/sendmail -t", "w");
			if(sendmail) {
				fputs("From: MAILER-DAEMON\n", sendmail);
				if(bflag) {
					fprintf(sendmail, "To: %s\n", privdata->from);
					fprintf(sendmail, "Cc: %s\n", postmaster);
				} else
					fprintf(sendmail, "To: %s\n", postmaster);

				if(!pflag)
					for(to = privdata->to; *to; to++)
						fprintf(sendmail, "Cc: %s\n", *to);
				fputs("Subject: Virus intercepted\n\n", sendmail);

				if(bflag)
					fputs("A message you sent to\n\t", sendmail);
				else
					fprintf(sendmail, "A message sent from %s to\n\t", privdata->from);

				for(to = privdata->to; *to; to++)
					fprintf(sendmail, "%s\n", *to);
				fputs("contained a virus and has not been delivered.\n\t", sendmail);
				fputs(mess, sendmail);

				pclose(sendmail);
			}
		}

		smfi_setreply(ctx, "550", "5.7.1", "Virus detected by ClamAV - http://clamav.elektrapro.com");
		rc = SMFIS_REJECT;
		free(err);
	}
	clamfi_cleanup(ctx);

	return rc;
}

static sfsistat
clamfi_abort(SMFICTX *ctx)
{
#ifdef	CL_DEBUG
	if(use_syslog)
		syslog(LOG_DEBUG, "clamfi_abort");
	puts("clamfi_abort");
#endif

	/*
	 * Unlock incase we're called during a cond_timedwait in envfrom
	 *
	 * TODO: There *must* be a tidier way of doing this!
	 */
	(void)pthread_mutex_unlock(&n_children_mutex);

	clamfi_cleanup(ctx);

	return SMFIS_TEMPFAIL;
}

static sfsistat
clamfi_close(SMFICTX *ctx)
{
#ifdef	CL_DEBUG
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	puts("clamfi_close");
	assert(privdata == NULL);
#endif

	if(logVerbose)
		syslog(LOG_DEBUG, "clamfi_close");

	return SMFIS_CONTINUE;
}

static void
clamfi_cleanup(SMFICTX *ctx)
{
	struct privdata *privdata = (struct privdata *)smfi_getpriv(ctx);

	assert(privdata != NULL);

	if(privdata->dataSocket >= 0) {
		close(privdata->dataSocket);
		privdata->dataSocket = -1;
	}

	if(privdata->from) {
#ifdef	CL_DEBUG
		if(debug_level >= 9)
			puts("Free privdata->from");
#endif
		free(privdata->from);
		privdata->from = NULL;
	}

	if(privdata->to) {
		char **to;

		for(to = privdata->to; *to; to++) {
#ifdef	CL_DEBUG
			if(debug_level >= 9)
				puts("Free *privdata->to");
#endif
			free(*to);
		}
#ifdef	CL_DEBUG
		if(debug_level >= 9)
			puts("Free privdata->to");
#endif
		free(privdata->to);
		privdata->to = NULL;
	}

	if(privdata->cmdSocket >= 0) {
		char buf[64];

		/*
		 * Flush the remote end so that clamd doesn't get a SIGPIPE
		 */
		while(recv(privdata->cmdSocket, buf, sizeof(buf), 0) > 0)
			;
		close(privdata->cmdSocket);
		privdata->cmdSocket = -1;
	}

#ifdef	CL_DEBUG
	if(debug_level >= 9)
		puts("Free privdata");
#endif
	free(privdata);
	smfi_setpriv(ctx, NULL);

	if(max_children > 0) {
		pthread_mutex_lock(&n_children_mutex);
		/*
		 * Deliberately errs on the side of broadcasting too many times
		 */
		--n_children;
		if((n_children < max_children) && (n_children > 0)) {
#ifdef	CL_DEBUG
			puts("pthread_cond_broadcast");
#endif
			if(pthread_cond_broadcast(&n_children_cond) < 0)
				perror("pthread_cond_broadcast");
		}
#ifdef	CL_DEBUG
		printf("<n_children = %d\n", n_children);
#endif
		pthread_mutex_unlock(&n_children_mutex);
	}
}

static int
clamfi_send(const struct privdata *privdata, size_t len, const char *format, ...)
{
	char output[BUFSIZ];
	const char *ptr;

	assert(format != NULL);

	if(len > 0)
		/*
		 * It isn't a NUL terminated string. We have a set number of
		 * bytes to output.
		 */
		ptr = format;
	else {
		va_list argp;

		va_start(argp, format);
		vsnprintf(output, sizeof(output), format, argp);
		va_end(argp);

		len = strlen(output);
		ptr = output;
	}
#ifdef	CL_DEBUG
	if(debug_level >= 9)
		printf("clamfi_send: len=%u bufsiz=%u\n", len, sizeof(output));
#endif

	while(len > 0) {
		int nbytes = send(privdata->dataSocket, ptr, len, 0);

		if(nbytes == -1) {
			if(errno == EINTR)
				continue;
			perror("send");
			if(use_syslog)
				syslog(LOG_ERR, "write failure to clamd");

			return -1;
		}
		len -= nbytes;
		ptr = &ptr[nbytes];
	}
	return 0;
}

/*
 * Like strcpy, but return the END of the destination, allowing a quicker
 * means of adding to the end of a string than strcat
 */
static char *
strrcpy(char *dest, const char *source)
{
	/* Pre assertions */
	assert(dest != NULL);
	assert(source != NULL);
	assert(dest != source);

	while((*dest++ = *source++) != '\0')
		;
	return(--dest);
}
