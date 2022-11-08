// file      : bdep/diagnostics.hxx -*- C++ -*-
// license   : MIT; see accompanying LICENSE file

#ifndef BDEP_DIAGNOSTICS_HXX
#define BDEP_DIAGNOSTICS_HXX

#include <utility> // move(), forward()

#include <odb/tracer.hxx>

#include <libbutl/diagnostics.hxx>

#include <bdep/types.hxx> // Note: not <bdep/utility.hxx>

namespace bdep
{
  using butl::diag_record;

  // Throw this exception to terminate the process. The handler should
  // assume that the diagnostics has already been issued.
  //
  class failed: public std::exception {};

  // Print process commmand line. If the number of elements is specified
  // (or the second version is used), then it will print the piped multi-
  // process command line, if present. In this case, the expected format
  // is as follows:
  //
  // name1 arg arg ... nullptr
  // name2 arg arg ... nullptr
  // ...
  // nameN arg arg ... nullptr nullptr
  //
  void
  print_process (diag_record&, const char* const args[], size_t n = 0);

  void
  print_process (const char* const args[], size_t n = 0);

  inline void
  print_process (diag_record& dr, const cstrings& args)
  {
    print_process (dr, args.data (), args.size ());
  }

  inline void
  print_process (const cstrings& args)
  {
    print_process (args.data (), args.size ());
  }

  // Verbosity level. Update documentation for --verbose if changing.
  //
  // 0 - disabled
  // 1 - high-level information messages
  // 2 - essential underlying commands that are being executed
  // 3 - all underlying commands that are being executed
  // 4 - information that could be helpful to the user
  // 5 - information that could be helpful to the developer
  // 6 - even more detailed information
  //
  // While uint8 is more than enough, use uint16 for the ease of printing.
  //
  extern uint16_t verb;

  template <typename F> inline void l1 (const F& f) {if (verb >= 1) f ();}
  template <typename F> inline void l2 (const F& f) {if (verb >= 2) f ();}
  template <typename F> inline void l3 (const F& f) {if (verb >= 3) f ();}
  template <typename F> inline void l4 (const F& f) {if (verb >= 4) f ();}
  template <typename F> inline void l5 (const F& f) {if (verb >= 5) f ();}
  template <typename F> inline void l6 (const F& f) {if (verb >= 6) f ();}

  // Diagnostic facility, base infrastructure.
  //
  using butl::diag_stream;
  using butl::diag_epilogue;

  // Diagnostic facility, project specifics.
  //
  struct simple_prologue_base
  {
    explicit
    simple_prologue_base (const char* type, const char* name)
        : type_ (type), name_ (name) {}

    void
    operator() (const diag_record& r) const;

  private:
    const char* type_;
    const char* name_;
  };

  class location
  {
  public:
    // Zero lines or columns are not printed.
    //
    explicit
    location (string f, uint64_t l = 0, uint64_t c = 0)
        : file (std::move (f)), line (l), column (c) {}

    location () = default;

    bool
    empty () const {return file.empty ();}

    string file;
    uint64_t line;
    uint64_t column;
  };

  struct location_prologue_base
  {
    location_prologue_base (const char* type,
                            const char* name,
                            const location& l)
        : type_ (type), name_ (name), loc_ (l) {}

    location_prologue_base (const char* type,
                            const char* name,
                            const path& f)
        : type_ (type), name_ (name), loc_ (f.string ()) {}

    void
    operator() (const diag_record& r) const;

  private:
    const char* type_;
    const char* name_;
    const location loc_;
  };

  struct basic_mark_base
  {
    using simple_prologue   = butl::diag_prologue<simple_prologue_base>;
    using location_prologue = butl::diag_prologue<location_prologue_base>;

    explicit
    basic_mark_base (const char* type,
                     const char* name = nullptr,
                     const void* data = nullptr,
                     diag_epilogue* epilogue = nullptr)
        : type_ (type), name_ (name), data_ (data), epilogue_ (epilogue) {}

    simple_prologue
    operator() () const
    {
      return simple_prologue (epilogue_, type_, name_);
    }

    location_prologue
    operator() (const location& l) const
    {
      return location_prologue (epilogue_, type_, name_, l);
    }

    location_prologue
    operator() (const path& f) const
    {
      return location_prologue (epilogue_, type_, name_, f);
    }

    template <typename L>
    location_prologue
    operator() (const L& l) const
    {
      // get_location() is the user-supplied ADL-searched function.
      //
      return location_prologue (
        epilogue_, type_, name_, get_location (l, data_));
    }

    template <typename F, typename L, typename C>
    location_prologue
    operator() (F&& f, L&& l, C&& c) const
    {
      return location_prologue (
        epilogue_,
        type_,
        name_,
        location (std::forward<F> (f),
                  std::forward<L> (l),
                  std::forward<C> (c)));
    }

  protected:
    const char* type_;
    const char* name_;
    const void* data_;
    diag_epilogue* const epilogue_;
  };
  using basic_mark = butl::diag_mark<basic_mark_base>;

  extern const basic_mark error;
  extern const basic_mark warn;
  extern const basic_mark info;
  extern const basic_mark text;

  // trace
  //
  // Also implement the ODB tracer interface so that we can use it to trace
  // database statement execution.
  //
  struct trace_mark_base: basic_mark_base, odb::tracer
  {
    explicit
    trace_mark_base (const char* name, const void* data = nullptr)
        : basic_mark_base ("trace", name, data) {}

    // odb::tracer interface.
    //
    virtual void
    prepare (odb::connection&, const odb::statement&);

    virtual void
    execute (odb::connection&, const char* statement);

    virtual void
    deallocate (odb::connection&, const odb::statement&);
  };
  using trace_mark = butl::diag_mark<trace_mark_base>;

  class tracer: public trace_mark
  {
  public:
    using trace_mark::trace_mark;

    // process_run_callback() command tracer interface.
    //
    void
    operator() (const char* const [], std::size_t) const;
  };

  // fail
  //
  struct fail_mark_base: basic_mark_base
  {
    explicit
    fail_mark_base (const char* type, const void* data = nullptr)
        : basic_mark_base (type,
                           nullptr,
                           data,
                           [](const diag_record& r, butl::diag_writer* w)
                           {
                             r.flush (w);
                             throw failed ();
                           }) {}
  };

  using fail_mark = butl::diag_mark<fail_mark_base>;

  struct fail_end_base
  {
    [[noreturn]] void
    operator() (const diag_record& r) const
    {
      // If we just throw then the record's destructor will see an active
      // exception and will not flush the record.
      //
      r.flush ();
      throw failed ();
    }
  };
  using fail_end = butl::diag_noreturn_end<fail_end_base>;

  extern const fail_mark fail;
  extern const fail_end  endf;
}

#endif // BDEP_DIAGNOSTICS_HXX
