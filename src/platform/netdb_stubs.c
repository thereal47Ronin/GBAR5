/* PSXS5 - the two network-database lookups libnfs makes, which the PS5's
 * C library doesn't have: the console has no /etc/protocols or /etc/services.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#if defined(__PROSPERO__) && defined(PSXS5_HAVE_NETFS)

#include <netdb.h>
#include <string.h>

struct protoent *getprotobyname(const char *name)
{
    static char *aliases[1] = {NULL};
    static struct protoent entry;
    if (!name)
        return NULL;
    entry.p_name = (char *)name;
    entry.p_aliases = aliases;
    entry.p_proto = !strcmp(name, "udp") ? 17 : !strcmp(name, "tcp") ? 6 : 0;
    return entry.p_proto ? &entry : NULL;
}

struct servent *getservbyport(int port, const char *proto)
{
    (void)port, (void)proto;
    return NULL; /* no service names here: libnfs then uses the number */
}

#endif
