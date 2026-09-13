#pragma once

// One global gate every HTTPS call in the firmware acquires before opening
// a connection and releases when done - see NetGate::Lock below.
//
// Several independent background tasks (weather, flight, calendar, notes,
// quote, checkin, stock, ...) each already wait for enough free heap before
// their OWN TLS handshake (see e.g. DashboardScreen::hourlyTaskEntry() and
// fetchStockQuote()), but that only protects a task against colliding with
// itself. Nothing stopped two DIFFERENT tasks from both passing their own
// heap check at the same moment and then racing mbedTLS for the same
// limited contiguous memory together - which is what "SSL - Memory
// allocation failed" actually was, and why it showed up on whichever
// symbol/day/feed happened to be mid-fetch when some other subsystem's task
// also started a handshake. Capping concurrent HTTPS connections at 1 for
// the whole firmware removes that race entirely: whichever caller asks
// first finishes its handshake before the next one is even allowed to
// start, no matter which subsystem it belongs to.
namespace NetGate {

    // Must be called once, before any fetch could possibly run - main.cpp's
    // setup(), early. A Lock taken before this runs just proceeds
    // unguarded rather than crashing, so call order mistakes fail soft.
    void begin();

    // RAII: acquires in the constructor (blocks until the slot is free),
    // releases in the destructor. Declare one as a local at the top of any
    // function that opens an HTTPClient connection - every early-return
    // path those functions already have (bad token, 404, JSON error, ...)
    // then releases the gate automatically, with no explicit release() call
    // to forget at any of them.
    class Lock {
    public:
        Lock();
        ~Lock();
    private:
        Lock(const Lock&) = delete;
        Lock& operator=(const Lock&) = delete;
    };

} // namespace NetGate
