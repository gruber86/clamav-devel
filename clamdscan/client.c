/*
 *  Copyright (C) 2002 - 2007 Tomasz Kojm <tkojm@clamav.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 *  MA 02110-1301, USA.
 */

#if HAVE_CONFIG_H
#include "clamav-config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_LIMITS_H
#include <sys/limits.h>
#endif
#include <sys/select.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <utime.h>
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#include "shared/optparser.h"
#include "shared/output.h"
#include "shared/misc.h"
#include "libclamav/str.h"
#include "libclamav/others.h"

#include "client.h"

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif

int notremoved = 0, notmoved = 0;
int printinfected = 0;

static struct sockaddr *mainsa = NULL;
static int mainsasz;
static struct sockaddr_un nixsock;
static struct sockaddr_in tcpsock;
enum {
    CONT,
    MULTI,
    STREAM,
    FILDES
};

static const char *scancmd[] = { "CONTSCAN", "MULTISCAN" };

static void (*action)(const char *) = NULL;
static char *actarget;
static void move_infected(const char *filename, int move);
static void action_move(const char *filename) {
    move_infected(filename, 1);
}
static void action_copy(const char *filename) {
    move_infected(filename, 0);
}
static void action_remove(const char *filename) {
    if(unlink(filename)) {
	logg("!%s: Can't remove.\n", filename);
	notremoved++;
    } else {
	logg("~%s: Removed.\n", filename);
    }
}

void actsetup(const struct optstruct *opts) {
    if(optget(opts, "move")->enabled) {
	actarget = optget(opts, "move")->strarg;
	action = action_move;
    } else if (optget(opts, "copy")->enabled) {
	actarget = optget(opts, "copy")->strarg;
	action = action_copy;
    } else if(optget(opts, "remove")->enabled) {
	action = action_remove;
    }
}

static int sendln(int sockd, const char *line, unsigned int len) {
    while(len) {
	int sent = send(sockd, line, len, 0);
	if(sent <= 0) {
	    if(sent && errno == EINTR) continue;
	    logg("!Can't send request to clamd\n");
	    return 1;
	}
	line += sent;
	len -= sent;
    }
    return 0;
}

struct RCVLN {
    char buf[PATH_MAX+1024]; /* FIXME must match that in clamd - bb1349 */
    int sockd;
    int r;
    char *cur;
    char *bol;
};

static void recvlninit(struct RCVLN *s, int sockd) {
    s->sockd = sockd;
    s->bol = s->cur = s->buf;
    s->r = 0;
}

static int recvln(struct RCVLN *s, char **rbol, char **reol) {
    char *eol;

    while(1) {
	if(!s->r) {
	    s->r = recv(s->sockd, s->cur, sizeof(s->buf) - (s->cur - s->buf), 0);
	    if(s->r<=0) {
		if(s->r && errno == EINTR) {
		    s->r = 0;
		    continue;
		}
		if(s->r || s->cur!=s->buf) {
		    logg("!Communication error\n");
		    return -1;
		}
	        return 0;
	    }
	}
	if((eol = memchr(s->cur, 0, s->r))) {
	    int ret = 0;
	    eol++;
	    s->r -= eol - s->cur;
	    *rbol = s->bol;
	    if(reol) *reol = eol;
	    ret = eol - s->bol;
	    if(s->r)
		s->bol = s->cur = eol;
	    else
		s->bol = s->cur = s->buf;
	    return ret;
	}
	s->r += s->cur - s->bol;
	if(!eol && s->r==sizeof(s->buf)) {
	    logg("!Overlong reply from clamd\n");
	    return -1;
	}
	if(!eol) {
	    if(s->buf != s->bol) { /* old memmove sux */
		memmove(s->buf, s->bol, s->r);
		s->bol = s->buf;
	    }
	    s->cur = &s->bol[s->r];
	    s->r = 0;
	}
    }
}

static int send_stream(int sockd, const char *filename) {
    uint32_t buf[BUFSIZ/sizeof(uint32_t)];
    int fd, len;

    if(filename) {
	if(!(fd = open(filename, O_RDONLY))) {
	    logg("!Open failed on %s.\n", filename);
	    return 1;
	}
    } else fd = 0;

    if(sendln(sockd, "zINSTREAM", 10)) return 1;

    while((len = read(fd, &buf[1], sizeof(buf) - sizeof(uint32_t))) > 0) {
	buf[0] = htonl(len);
	if(sendln(sockd, (const char *)buf, len+sizeof(uint32_t))) { /* FIXME: need to handle limits */
	    logg("!Can't write to the socket.\n");
	    close(fd);
	    return 1;
	}
    }
    close(fd);
    if(len) {
	logg("!Failed to read from %s.\n", filename);
	return 1;
    }
    return 0;
}

static int send_fdpass(int sockd, const char *filename) {
    struct iovec iov[1];
    struct msghdr msg;
    struct cmsghdr *cmsg;
    unsigned char fdbuf[CMSG_SPACE(sizeof(int))];
    char dummy[]="";
    int fd;

    if(filename) {
	if(!(fd = open(filename, O_RDONLY))) {
	    logg("!Open failed on %s.\n", filename);
	    return 1;
	}
    } else fd = 0;
    if(sendln(sockd, "zFILDES", 8)) return 1;

    iov[0].iov_base = dummy;
    iov[0].iov_len = 1;
    memset(&msg, 0, sizeof(msg));
    msg.msg_control = fdbuf;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_controllen = CMSG_LEN(sizeof(int));
    cmsg = CMSG_FIRSTHDR(&msg);
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    *(int *)CMSG_DATA(cmsg) = fd;
    if(sendmsg(sockd, &msg, 0) == -1) {
	logg("!FD send failed\n");
	return 1;
    }
    return 0;
}

static int dsresult(int sockd, int scantype, const char *filename)
{
	int infected = 0, waserror = 0, len;
	char *bol, *eol;
	struct RCVLN rcv;

    recvlninit(&rcv, sockd);

    switch(scantype) {
    case MULTI:
    case CONT:
    len = strlen(filename) + strlen(scancmd[scantype]) + 3;
    if (!(bol = malloc(len))) {
	logg("!Cannot allocate a command buffer\n");
	return -1;
    }
    sprintf(bol, "z%s %s", scancmd[scantype], filename);
    if(sendln(sockd, bol, len)) return -1;
    free(bol);
    break;

    case STREAM:
	if(send_stream(sockd, filename))
	    return -1;

#ifdef HAVE_FD_PASSING
    case FILDES:
	if(send_fdpass(sockd, filename))
	    return -1;
	break;
#endif
    }

    while((len = recvln(&rcv, &bol, &eol))) {
	if(len == -1) {
	    waserror = 1;
	    break;
	}
	if(!filename) logg("~%s\n", bol);
	if(len > 7) {
	    char *colon = colon = strrchr(bol, ':');
	    if(!colon) {
		logg("Failed to parse reply\n");
		waserror = 1;
	    } else if(!memcmp(eol - 7, " FOUND", 6)) {
		infected++;
		if(filename) {
		    if(scantype >= STREAM) {
			logg("~%s%s\n", filename, colon);
			if(action) action(filename);
		    } else {
			logg("~%s\n", bol);
			*colon = '\0';
			if(action)
			    action(bol);
		    }
		}
	    } else if(!memcmp(eol-7, " ERROR", 6)) {
		if(filename) {
		    if(scantype >= STREAM)
			logg("~%s%s\n", filename, colon);
		    else
			logg("~%s\n", bol);
		}
		waserror = 1;
	    }
	}
    }
    return infected ? infected : (waserror ? -1 : 0);
}


#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

static int dconnect()
{
	int sockd;

    if((sockd = socket(mainsa->sa_family, SOCK_STREAM, 0)) < 0) {
	perror("socket()");
	logg("!Can't create the socket.\n");
	return -1;
    }

    if(connect(sockd, (struct sockaddr *)mainsa, mainsasz) < 0) {
	close(sockd);
	perror("connect()");
	logg("!Can't connect to clamd.\n");
	return -1;
    }

    return sockd;
}

static int isremote(const struct optstruct *opts) {
    int s, ret;
    const struct optstruct *opt;
    struct hostent *he;
    struct optstruct *clamdopts;
    const char *clamd_conf = optget(opts, "config-file")->strarg;
    static struct sockaddr_in testsock;

    if((clamdopts = optparse(clamd_conf, 0, NULL, 1, OPT_CLAMD, 0, NULL)) == NULL) {
	logg("!Can't parse clamd configuration file %s\n", clamd_conf);
	return 0;
    }
    if((opt = optget(clamdopts, "LocalSocket"))->enabled) {
	memset((void *)&nixsock, 0, sizeof(nixsock));
	nixsock.sun_family = AF_UNIX;
	strncpy(nixsock.sun_path, opt->strarg, sizeof(nixsock.sun_path));
	nixsock.sun_path[sizeof(nixsock.sun_path)-1]='\0';
	mainsa = (struct sockaddr *)&nixsock;
	mainsasz = sizeof(nixsock);
	optfree(clamdopts);
	return 0;
    }
    if(!(opt = optget(clamdopts, "TCPSocket"))->enabled) {
	optfree(clamdopts);
	return 0;
    }
    mainsa = (struct sockaddr *)&tcpsock;
    mainsasz = sizeof(tcpsock);
    memset((void *)&tcpsock, 0, sizeof(tcpsock));
    tcpsock.sin_family = AF_INET;
    tcpsock.sin_port = htons(opt->numarg);
    if(!(opt = optget(clamdopts, "TCPAddr"))->enabled) {
	tcpsock.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	optfree(clamdopts);
	return 0;
    }
    he = gethostbyname(opt->strarg);
    optfree(clamdopts);
    if(!he) {
	perror("gethostbyname()");
	logg("!Can't lookup clamd hostname.\n");
	mainsa = NULL;
	return 0;
    }
    tcpsock.sin_addr = *(struct in_addr *) he->h_addr_list[0];
    memcpy((void *)&testsock, (void *)&tcpsock, sizeof(testsock));
    testsock.sin_port = htons(INADDR_ANY);
    if(!(s = socket(testsock.sin_family, SOCK_STREAM, 0))) return 0;
    ret = (bind(s, (struct sockaddr *)&testsock, sizeof(testsock)) != 0);
    close(s);
    return ret;
}

struct client_serial_data {
    int infected;
    int errors;
    int scantype;
    int spam;
};

static int serial_callback(struct stat *sb, char *filename, const char *path, enum cli_ftw_reason reason, struct cli_ftw_cbdata *data) {
    struct client_serial_data *c = (struct client_serial_data *)data->data;
    int sockd, ret;
    const char *f = filename;

    switch(reason) {
    case error_stat:
	logg("^Can't access file %s\n", filename);
	return CL_SUCCESS;
    case error_mem:
	logg("^Memory allocation failed in ftw\n");
	return CL_EMEM;
    case warning_skipped_dir:
	logg("^Directory recursion limit reached\n");
	return CL_SUCCESS;
    case warning_skipped_special:
	logg("~%s: Not supported file type. ERROR\n", filename);
	c->errors++;
	return CL_SUCCESS;
    default:
	break;
    }

    if(reason == visit_directory_toplev) {
	c->spam = 1;
	if(c->scantype >= STREAM) {
	    free(filename);
	    return CL_SUCCESS;
	}
	f = path;
    }
    if((sockd = dconnect()) < 0) {
	free(filename);
	return CL_BREAK;
    }
    if((ret = dsresult(sockd, c->scantype, f)) >= 0)
	c->infected += ret;
    else
	c->errors++;
    close(sockd);
    free(filename);
    if(reason == visit_directory_toplev)
	return CL_BREAK;
    return CL_SUCCESS;
}

static char *makeabs(const char *basepath) {
    int namelen;
    char *ret;

    if(!(ret = malloc(PATH_MAX + 1))) {
	logg("^Can't make room for fullpath.\n");
	return NULL;
    }
    if(*basepath != '/') { /* FIXME: to be unified */
	if(!getcwd(ret, PATH_MAX)) {
	    logg("^Can't get absolute pathname of current working directory.\n");
	    free(ret);
	    return NULL;
	}
	namelen = strlen(ret);
	snprintf(&ret[namelen], PATH_MAX - namelen, "/%s", basepath);
    } else {
	strncpy(ret, basepath, PATH_MAX);
    }
    ret[PATH_MAX] = '\0';
    return ret;
}

static int serial_client_scan(const char *file, int scantype, int *infected, int *errors, int maxlevel) {
    struct cli_ftw_cbdata data;
    struct client_serial_data cdata;

    cdata.infected = 0;
    cdata.errors = 0;
    cdata.scantype = scantype;
    cdata.spam = 0;
    data.data = &cdata;

    cli_ftw(file, CLI_FTW_STD, maxlevel ? maxlevel : INT_MAX, serial_callback, &data);
    if(!cdata.infected && (!cdata.errors || cdata.spam)) logg("~%s: OK\n", file);

    *infected += cdata.infected;
    *errors += cdata.errors;
    return 0;
}

struct client_parallel_data {
    int infected;
    int errors;
    int scantype;
    int spam;
    int sockd;
    int lastid;
    struct SCANID {
	unsigned int id;
	const char *file;
	struct SCANID *next;
    } *ids;
};

int dspresult(struct client_parallel_data *c) {
    int rid, len;
    char *bol, *eol, *filename;
    fd_set fds;
    struct SCANID **id = NULL;
    struct timeval tv = { 0, 0 };
    struct RCVLN rcv;

    recvlninit(&rcv, c->sockd);
    while(1) {
	FD_ZERO(&fds);
	FD_SET(c->sockd, &fds);
	switch (select(1, &fds, NULL, NULL, &tv)) {
	case -1:
	    c->errors++;
	    logg("!select failed during session\n");
	    return CL_BREAK; /* this is an hard failure */
	case 0:
	    c->errors++;
	    return CL_SUCCESS;
	}

	/* FIXME: repeat till i have stuff in the buff */
	len = recvln(&rcv, &bol, &eol);
	if(len == -1) {
	    c->errors++;
	    break;
	}
	if((rid = atoi(bol))) {
	    id = &c->ids;
	    while(*id) {
		if((*id)->id == rid) break;
		id = &((*id)->next);
	    }
	    if(!*id) id = NULL;
	}
	if(!id) {
	    c->errors++;
	    logg("!Bogus session id from clamd\n");
	    return CL_BREAK; /* this is an hard failure */
	}
	filename = (*id)->file;
	if(len > 7) {
	    char *colon = colon = strrchr(bol, ':');
	    if(!colon) {
		c->errors++;
		logg("Failed to parse reply\n");
	    } else if(!memcmp(eol - 7, " FOUND", 6)) {
		c->infected++;
		logg("~%s%s\n", filename, colon);
		if(action) action(filename);
	    } else if(!memcmp(eol-7, " ERROR", 6)) {
		c->errors++;
		if(filename)
		    logg("~%s%s\n", filename, colon);
	    }
	}
    }
    return CL_SUCCESS;
}


static int parallel_callback(struct stat *sb, char *filename, const char *path, enum cli_ftw_reason reason, struct cli_ftw_cbdata *data) {
    struct client_parallel_data *c = (struct client_parallel_data *)data->data;
    struct SCANID **id = &c->ids, *cid;

    switch(reason) {
    case error_stat:
	logg("^Can't access file %s\n", filename);
	return CL_SUCCESS;
    case error_mem:
	logg("^Memory allocation failed in ftw\n");
	return CL_EMEM;
    case warning_skipped_dir:
	logg("^Directory recursion limit reached\n");
	return CL_SUCCESS;
    case warning_skipped_special:
	logg("~%s: Not supported file type. ERROR\n", filename);
	c->errors++;
	return CL_SUCCESS;
    case visit_directory_toplev:
	c->spam = 1;
	free(filename);
	return CL_SUCCESS;
    default:
	break;
    }

    while (*id)
	id = &((*id)->next);
    cid = (struct SCANID *)malloc(sizeof(struct SCANID *));
    *id = cid;
    cid->id = ++c->lastid;
    cid->file = filename;
    cid->next = NULL;

    switch(c->scantype) {
    case FILDES:
	send_fdpass(c->sockd, filename); /* FIXME: check return */
	break;
    case STREAM:
	send_stream(c->sockd, filename); /* FIXME: check return */
	break;
    }

    return dspresult(c);
}

static int parallel_client_scan(const char *file, int scantype, int *infected, int *errors, int maxlevel) {
    struct cli_ftw_cbdata data;
    struct client_parallel_data cdata;

    if((cdata.sockd = dconnect()) < 0)
	return 1;

    if(sendln(cdata.sockd, "zIDSESSION", 11)) {
	close(cdata.sockd);
	return 1;
    }

    cdata.infected = 0;
    cdata.errors = 0;
    cdata.scantype = scantype;
    cdata.spam = 0;
    cdata.lastid = 0;
    data.data = &cdata;

    cli_ftw(file, CLI_FTW_STD, maxlevel ? maxlevel : INT_MAX, parallel_callback, &data);
    /* FIXME: check return */
    if(!cdata.infected && (!cdata.errors || cdata.spam)) logg("~%s: OK\n", file);

    *infected += cdata.infected;
    *errors += cdata.errors;
    return 0;
}

static int client_scan(const char *file, int scantype, int *infected, int *errors, int maxlevel, int session) {
    int ret;
    char *fullpath = makeabs(file);

    if(!fullpath) {
	(*errors)++;
	return 1;
    }
    if (!session)
	ret = serial_client_scan(fullpath, scantype, infected, errors, maxlevel);
    else
	ret = parallel_client_scan(fullpath, scantype, infected, errors, maxlevel);
    free(fullpath);
    return 0;
}

int get_clamd_version(const struct optstruct *opts)
{
	char *buff;
	int len, sockd;
	struct RCVLN rcv;

    isremote(opts);
    if(!mainsa) return 2;
    if((sockd = dconnect()) < 0) return 2;
    recvlninit(&rcv, sockd);

    if(sendln(sockd, "zVERSION", 9)) {
	close(sockd);
	return 2;
    }

    while((len = recvln(&rcv, &buff, NULL))) {
	if(len == -1) {
	    logg("!Error occoured while receiving version information.\n");
	    break;
	}
	printf("%s\n", buff);
    }

    close(sockd);
    return 0;
}

int reload_clamd_database(const struct optstruct *opts)
{
	char *buff;
	int len, sockd;
	struct RCVLN rcv;

    isremote(opts);
    if(!mainsa) return 2;
    if((sockd = dconnect()) < 0) return 2;
    recvlninit(&rcv, sockd);

    if(sendln(sockd, "zRELOAD", 8)) {
	close(sockd);
	return 2;
    }

    if(!(len = recvln(&rcv, &buff, NULL)) || len < 10 || memcmp(buff, "RELOADING", 9)) {
	logg("!Clamd did not reload the database\n");
	close(sockd);
	return 2;
    }
    close(sockd);
    return 0;
}

int client(const struct optstruct *opts, int *infected)
{
	const char *clamd_conf = optget(opts, "config-file")->strarg;
	struct optstruct *clamdopts;
	int remote, scantype, session = 0, errors = 0, scandash = 0, maxrec;

    if((clamdopts = optparse(clamd_conf, 0, NULL, 1, OPT_CLAMD, 0, NULL)) == NULL) {
	logg("!Can't parse clamd configuration file %s\n", clamd_conf);
	return 2;
    }

    scandash = (opts->filename && opts->filename[0] && !strcmp(opts->filename[0], "-") && !opts->filename[1]);
    remote = isremote(opts);
#ifdef HAVE_FD_PASSING
    if(!remote && optget(clamdopts, "LocalSocket")->enabled && (optget(opts, "fdpass")->enabled || scandash)) {
	scantype = FILDES;
	session = optget(opts, "multiscan")->enabled;
    } else 
#endif
    if(remote || scandash) {
	scantype = STREAM;
	session = optget(opts, "multiscan")->enabled;
    } else if(optget(opts, "multiscan")->enabled) scantype = MULTI;
    else scantype = CONT;

    maxrec = optget(clamdopts, "MaxDirectoryRecursion")->numarg;
    optfree(clamdopts);

    if(!mainsa) {
	logg("!Clamd is not configured properly.\n");
	return 2;
    }

    *infected = 0;

    if(scandash) {
	int sockd, ret;
	struct stat sb;
	fstat(0, &sb);
	if((sb.st_mode & S_IFMT) != S_IFREG) scantype = STREAM;
	if((sockd = dconnect()) >= 0 && (ret = dsresult(sockd, scantype, NULL)) >= 0)
	    *infected = ret;
	else
	    errors++;
	if(sockd >= 0) close(sockd);
    } else if(opts->filename) {
	unsigned int i;
	for (i = 0; opts->filename[i]; i++) {
	    if(!strcmp(opts->filename[i], "-")) {
		logg("!Scanning from standard input requires \"-\" to be the only file argument\n");
		continue;
	    }
	    if(client_scan(opts->filename[i], scantype, infected, &errors, maxrec, session)) break;
	}
    } else {
	client_scan("", scantype, infected, &errors, maxrec, session);
    }
    return *infected ? 1 : (errors ? 2 : 0);
}

void move_infected(const char *filename, int move)
{
	char *movefilename, numext[4 + 1];
	const char *tmp;
	struct stat ofstat, mfstat;
	int n, len, movefilename_size;
	struct utimbuf ubuf;

    if(access(actarget, W_OK|X_OK) == -1) {
        logg("!problem %s file '%s': cannot write to '%s': %s\n", (move) ? "moving" : "copying", filename, actarget, strerror(errno));
        notmoved++;
        return;
    }

    if(stat(filename, &ofstat) == -1) {
        logg("^Can't stat file %s\n", filename);
	logg("Try to run clamdscan with clamd privileges\n");
        notmoved++;
	return;
    }

    if(!(tmp = strrchr(filename, '/')))
	tmp = filename;

    movefilename_size = sizeof(char) * (strlen(actarget) + strlen(tmp) + sizeof(numext) + 2);

    if(!(movefilename = malloc(movefilename_size))) {
        logg("!Memory allocation error\n");
	exit(2);
    }

    if(!(cli_strrcpy(movefilename, actarget))) {
        logg("!cli_strrcpy() returned NULL\n");
        notmoved++;
        free(movefilename);
        return;
    }

    strcat(movefilename, "/");

    if(!(strcat(movefilename, tmp))) {
        logg("!strcat() returned NULL\n");
        notmoved++;
        free(movefilename);
        return;
    }

    if(!stat(movefilename, &mfstat)) {
        if((ofstat.st_dev == mfstat.st_dev) && (ofstat.st_ino == mfstat.st_ino)) { /* It's the same file */
            logg("File excluded '%s'\n", filename);
            notmoved++;
            free(movefilename);
            return;
        } else {
            /* file exists - try to append an ordinal number to the
	     * quranatined file in an attempt not to overwrite existing
	     * files in quarantine  
	     */
            len = strlen(movefilename);
            n = 0;        		        		
            do {
                /* reset the movefilename to it's initial value by
		 * truncating to the original filename length
		 */
                movefilename[len] = 0;
                /* append .XXX */
                sprintf(numext, ".%03d", n++);
                strcat(movefilename, numext);            	
            } while(!stat(movefilename, &mfstat) && (n < 1000));
       }
    }

    if(!move || rename(filename, movefilename) == -1) {
	if(filecopy(filename, movefilename) == -1) {
	    logg("^cannot %s '%s' to '%s': %s\n", (move) ? "move" : "copy", filename, movefilename, strerror(errno));
	    notmoved++;
	    free(movefilename);
	    return;
	}

	chmod(movefilename, ofstat.st_mode);
	if(chown(movefilename, ofstat.st_uid, ofstat.st_gid) == -1)
	    logg("^chown() failed for %s: %s\n", movefilename, strerror(errno));

	ubuf.actime = ofstat.st_atime;
	ubuf.modtime = ofstat.st_mtime;
	utime(movefilename, &ubuf);

	if(move && unlink(filename)) {
	    logg("^cannot unlink '%s': %s\n", filename, strerror(errno));
	    notremoved++;
	    free(movefilename);
	    return;
	}
    }

    logg("%s: %s to '%s'\n", (move)?"moved":"copied", filename, movefilename);

    free(movefilename);
}
