// Event.h
#if !defined(_EVENT_H)
#define _EVENT_H

#include "IntrusiveBase.h"
#include "EventManager.h"

#include <list>
#include <string>
#include <boost/any.hpp>

class CycDupEventFieldErr: public CycException {
    public: CycDupEventFieldErr(std::string msg) : CycException(msg) {};
};

// Used to specify and send a collection of key-value pairs to the
// EventManager for recording.
class Event: IntrusiveBase<Event> { friend class EventManager;

  public:

    virtual ~Event();

    // Add an arbitrary labeled value to the event.
    //
    // @param field a label or key for a value. Loosely analogous to a column
    // label.
    //
    // @warning for the val argument - what val types are supported depends on
    // what the selected backend(s) handles.
    event_ptr addVal(std::string field, boost::any val);

    /// Record this event to its EventManager.
    void record();

    /// Returns the event's title as specified during the event's creation.
    std::string title();

    /// Returns a map of all field-value pairs that have been added to this event.
    ValMap vals();

    // Returns the full, unique name generated for this event using info about
    // its creator along with its title.
    std::string name();

  private:
    Event(EventManager* m, Model* creator, std::string title);
    bool schemaWithin(event_ptr primary);

    EventManager* manager_;
    std::string title_;
    int creator_id_;
    std::string creator_impl_;
    ValMap vals_;
};

#endif
