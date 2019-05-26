#include <assert.h>

#include <mrkcommon/bytes.h>
#include <mrkcommon/bytestream_aux.h>
#include <mrkcommon/dumpm.h>
#include <mrkcommon/util.h>

#include <mrkprotobuf.h>

#include "data/scalar-02.h"

#include "unittest.h"

#ifndef NDEBUG
const char *_malloc_options = "AJ";
#endif

static mnbytes_t _foo = BYTES_INITIALIZER("FOO");
static mnbytes_t _john = BYTES_INITIALIZER("John");
static mnbytes_t _doe = BYTES_INITIALIZER("Doe");


int
main(void)
{
    struct scalar_02 *sc0, *sc1;
    mnbytestream_t bs0, bs1;
    mnbytes_t *s;
    ssize_t sz;

    sc0 = scalar_02_new();
    assert(sc0 != NULL);
    sc1 = scalar_02_new();
    assert(sc1 != NULL);

    sc0->id = 123;
    sc0->name = &_foo;
    BYTES_INCREF(sc0->name);
    sc0->weight = 234;
    sc0->impression = EXCELLENT;
    sc0->person.first = &_john;
    BYTES_INCREF(sc0->person.first);
    sc0->person.last = &_doe;
    BYTES_INCREF(sc0->person.last);
    sc0->person.age = 123456789;

    (void)bytestream_init(&bs0, 32);

    sz = scalar_02_pack(&bs0, sc0);
    D8(SPDATA(&bs0), SEOD(&bs0));
    TRACE("sz=%zd", sz);

    s = bytes_new_from_mem_len(SPDATA(&bs0), SEOD(&bs0));
    bytestream_from_bytes(&bs1, s);
    SEOD(&bs1) = BSZ(s);
    //D8(BDATA(s), BSZ(s));

    sz = scalar_02_unpack(&bs1, NULL, sc1);
    TRACE("sz=%zd", sz);

    bytestream_rewind(&bs0);
    sz = scalar_02_dump(&bs0, sc0);
    //D8(SPDATA(&bs0), SEOD(&bs0));
    TRACE("dump: %s", SPDATA(&bs0));
    bytestream_rewind(&bs0);
    sz = scalar_02_dump(&bs0, sc1);
    //D8(SPDATA(&bs0), SEOD(&bs0));
    TRACE("dump: %s", SPDATA(&bs0));

    scalar_02_destroy(&sc0);
    assert(sc0 == NULL);
    scalar_02_destroy(&sc1);
    assert(sc1 == NULL);

    bytestream_fini(&bs0);
    //bytestream_fini(&bs1);
    BYTES_DECREF(&s);

    BYTES_NREF_STATIC_INVARIANT(_foo);
    BYTES_NREF_STATIC_INVARIANT(_john);
    BYTES_NREF_STATIC_INVARIANT(_doe);

    return 0;
}

