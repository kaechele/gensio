
#include <vector>
#include <cstring>
#include <gensio/gensio>

namespace gensio {
#include <gensio/gensio_builtins.h>

    void Event::write_ready(Gensio *io)
    {
	// Make sure we don't go into an infinite loop.
	io->set_write_callback_enable(false);
    }

    void Event::new_channel(Gensio *io, Gensio *new_channel,
			    const char *const *auxdata)
    {
	// No handler, just delete it.
	new_channel->free();
    }

    struct gensio_cpp_data {
	struct gensio_frdata frdata;
	Gensio *g;
    };

    Gensio *gensio_alloc(struct gensio *io, struct gensio_os_funcs *o,
			 struct Event *cb);

    static int gensio_cpp_cb(struct gensio *io, void *user_data,
			     int event, int err,
			     unsigned char *buf, gensiods *buflen,
			     const char *const *auxdata)
    {
	Gensio *g = static_cast<Gensio *>(user_data);
	Event *cb = g->get_cb();
	Gensio *g2;

	try {
	    if (event >= GENSIO_EVENT_USER_MIN &&
		event <= GENSIO_EVENT_USER_MAX)
		return cb->user_event(g, event, err, buf, buflen, auxdata);

	    if (event >= SERGENSIO_EVENT_BASE &&
			event <= SERGENSIO_EVENT_MAX) {
		Serial_Event *scb = dynamic_cast<Serial_Event *>(cb);
		unsigned int *val = (unsigned int *) buf;

		if (!scb)
		    return GE_NOTSUP;

		if (event == GENSIO_EVENT_SER_SIGNATURE) {
		    scb->signature((char *) buf, *buflen);
		    return 0;
		}

		switch (event) {
		case GENSIO_EVENT_SER_MODEMSTATE:
		    scb->modemstate(*val);
		    break;

		case GENSIO_EVENT_SER_LINESTATE:
		    scb->linestate(*val);
		    break;

		case GENSIO_EVENT_SER_FLOW_STATE:
		    scb->flow_state(*val);
		    break;

		case GENSIO_EVENT_SER_FLUSH:
		    scb->flush(*val);
		    break;

		case GENSIO_EVENT_SER_SYNC:
		    scb->sync();
		    break;

		case GENSIO_EVENT_SER_BAUD:
		    scb->baud(*val);
		    break;

		case GENSIO_EVENT_SER_DATASIZE:
		    scb->datasize(*val);
		    break;

		case GENSIO_EVENT_SER_PARITY:
		    scb->parity(*val);
		    break;

		case GENSIO_EVENT_SER_STOPBITS:
		    scb->stopbits(*val);
		    break;

		case GENSIO_EVENT_SER_FLOWCONTROL:
		    scb->flowcontrol(*val);
		    break;

		case GENSIO_EVENT_SER_IFLOWCONTROL:
		    scb->iflowcontrol(*val);
		    break;

		case GENSIO_EVENT_SER_SBREAK:
		    scb->sbreak(*val);
		    break;

		case GENSIO_EVENT_SER_DTR:
		    scb->dtr(*val);
		    break;

		case GENSIO_EVENT_SER_RTS:
		    scb->rts(*val);
		    break;

		default:
		    return GE_NOTSUP;
		}
		return 0;
	    }

	    switch (event) {
	    case GENSIO_EVENT_READ:
		return cb->read(g, err, buf, buflen, auxdata);

	    case GENSIO_EVENT_WRITE_READY:
		cb->write_ready(g);
		break;

	    case GENSIO_EVENT_NEW_CHANNEL:
		g2 = gensio_alloc(io, g->get_os_funcs(), NULL);
		cb->new_channel(g, g2, auxdata);
		break;

	    case GENSIO_EVENT_SEND_BREAK:
		cb->send_break(g);
		break;

	    case GENSIO_EVENT_AUTH_BEGIN:
		return cb->auth_begin(g);

	    case GENSIO_EVENT_PRECERT_VERIFY:
		return cb->precert_verify(g);

	    case GENSIO_EVENT_POSTCERT_VERIFY:
		return cb->postcert_verify(g);

	    case GENSIO_EVENT_PASSWORD_VERIFY:
		return cb->password_verify(g, buf);

	    case GENSIO_EVENT_REQUEST_PASSWORD:
		return cb->request_password(g, buf, buflen);
	    }
	    return GE_NOTSUP;
	} catch (std::exception e) {
	    gensio_log(g->get_os_funcs(), GENSIO_LOG_ERR,
		       "Received C++ exception in callback handler: %s",
		       e.what());
	    return GE_APPERR;
	}
    }

    void gensio_cpp_freed(struct gensio *io, struct gensio_frdata *frdata)
    {
	struct gensio_cpp_data *d = gensio_container_of(frdata,
							struct gensio_cpp_data,
							frdata);
	Event *cb = d->g->get_cb();

	if (cb)
	    cb->freed();
	delete d->g;
	delete d;
    }

    void
    Gensio::set_gensio(struct gensio *io)
    {
	struct gensio_cpp_data *d;

	try {
	    d = new struct gensio_cpp_data;
	} catch (...) {
	    delete this;
	    throw;
	}
	this->io = io;
	d->g = this;
	d->frdata.freed = gensio_cpp_freed;
	gensio_set_frdata(io, &d->frdata);
	gensio_set_callback(io, gensio_cpp_cb, this);
    }

    void
    Serial_Gensio::set_gensio(struct gensio *io)
    {
	this->sio = gensio_to_sergensio(io);
	Gensio::set_gensio(io);
    }

    Gensio *
    alloc_tcp_class(struct gensio_os_funcs *o)
    {
	return new Tcp(o);
    }

    Gensio *
    alloc_unix_class(struct gensio_os_funcs *o)
    {
	return new Unix(o);
    }

    Gensio *
    alloc_stdio_class(struct gensio_os_funcs *o)
    {
	return new Stdio(o);
    }

    Gensio *
    alloc_serialdev_class(struct gensio_os_funcs *o)
    {
	return new Serialdev(o);
    }

    struct gensio_class {
	const char *name;
	class Gensio *(*allocator)(struct gensio_os_funcs *o);
    };

    static std::vector<struct gensio_class> classes {
	{ "tcp", alloc_tcp_class },
	{ "unix", alloc_unix_class },
	{ "stdio", alloc_stdio_class },
	{ "serialdev", alloc_serialdev_class },
    };

    Gensio *
    gensio_alloc(struct gensio *io, struct gensio_os_funcs *o)
    {
	struct gensio *cio;
	struct sergensio *sio;
	unsigned int i;
	struct gensio_frdata *f;
	struct gensio_cpp_data *d;
	Gensio *g = NULL;

	// Set frdata for the gensio and all children.
	for (i = 0; cio = gensio_get_child(io, i); i++) {
	    if (gensio_get_frdata(cio))
		break; // It's already been set.
	    const char *type = gensio_get_type(cio, 0);
	    for (unsigned int i = 0; i < classes.size(); i++) {
		if (strcmp(type, classes[i].name) == 0) {
		    g = classes[i].allocator(o);
		    break;
		}
	    }
	    if (g == NULL) {
		sio = gensio_to_sergensio(cio);
		if (sio) {
		    g = new Serial_Gensio(o, NULL);
		} else {
		    g = new Gensio(o, NULL);
		}
	    }
	    g->set_gensio(cio);
	}
	f = gensio_get_frdata(io);
	d = gensio_container_of(f, struct gensio_cpp_data, frdata);
	return d->g;
    }

    Gensio *
    gensio_alloc(struct gensio *io, struct gensio_os_funcs *o, Event *cb)
    {
	Gensio *g;

	g = gensio_alloc(io, o);
	g->set_event_handler(cb);
	return g;
    }

    Gensio *
    gensio_alloc(std::string str, struct gensio_os_funcs *o, Event *cb)
    {
	struct gensio *io;
	int err;
	Gensio *g;

	err = str_to_gensio(str.c_str(), o, NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	g = gensio_alloc(io, o, cb);
	return g;
    }

    Gensio *
    gensio_alloc(Gensio *child, std::string str,
		 struct gensio_os_funcs *o, Event *cb)
    {
	struct gensio *io;
	int err;
	Gensio *g;

	err = str_to_gensio_child(child->io, str.c_str(), o,
				  NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	g = gensio_alloc(io, o, cb);
	return g;
    }

    Gensio::Gensio(struct gensio_os_funcs *o, Event *cb)
    {
	go = o;
	gcb = cb;
    }

    void Gensio::free()
    {
	gensio_free(io);
    }

    static void gensio_cpp_open_done(struct gensio *io, int err,
				     void *user_data)
    {
	struct gensio_frdata *f = gensio_get_frdata(io);
	struct gensio_cpp_data *d = gensio_container_of(f,
					      struct gensio_cpp_data, frdata);
	Gensio *g = d->g;
	Gensio_Done_Err *done = static_cast<Gensio_Done_Err *>(user_data);

	try {
	    done->done(g, err);
	} catch (std::exception e) {
	    gensio_log(g->get_os_funcs(), GENSIO_LOG_ERR,
		       "Received C++ exception in open done handler: %s",
		       e.what());
	}
    }

    void Gensio::open(Gensio_Done_Err *done)
    {
	int err;

	err = gensio_open(io, gensio_cpp_open_done, done);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::open_s()
    {
	int err = gensio_open_s(io);

	if (err)
	    throw gensio_error(err);
    }

    void Gensio::open_nochild(Gensio_Done_Err *done)
    {
	int err;

	err = gensio_open_nochild(io, gensio_cpp_open_done, done);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::open_nochild_s()
    {
	int err = gensio_open_nochild_s(io);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::write(gensiods *count, const void *buf, gensiods buflen,
		       const char *const *auxdata)
    {
	int err = gensio_write(io, count, buf, buflen, auxdata);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::write_sg(gensiods *count,
			  const struct gensio_sg *sg, gensiods sglen,
			  const char *const *auxdata)
    {
	int err = gensio_write_sg(io, count, sg, sglen, auxdata);
	if (err)
	    throw gensio_error(err);
    }

    int Gensio::write_s(gensiods *count, const void *data, gensiods datalen,
			 gensio_time *timeout)
    {
	int err = gensio_write_s(io, count, data, datalen, timeout);
	if (err == GE_TIMEDOUT)
	    return err;
	if (err)
	    throw gensio_error(err);
	return 0;
    }

    int Gensio::write_s_intr(gensiods *count, const void *data,
			     gensiods datalen, gensio_time *timeout)
    {
	int err = gensio_write_s_intr(io, count, data, datalen, timeout);
	if (err == GE_TIMEDOUT || err == GE_INTERRUPTED)
	    return err;
	if (err)
	    throw gensio_error(err);
	return 0;
    }

    Gensio *Gensio::alloc_channel(const char *const args[], Event *cb)
    {
	struct gensio *nio;
	int err = gensio_alloc_channel(io, args, NULL, NULL, &nio);
	Gensio *g;

	if (err)
	    throw gensio_error(err);
	g = gensio_alloc(nio, go, cb);
	return g;
    }

    static void gensio_cpp_close_done(struct gensio *io, void *user_data)
    {
	struct gensio_frdata *f = gensio_get_frdata(io);
	struct gensio_cpp_data *d = gensio_container_of(f,
					      struct gensio_cpp_data, frdata);
	Gensio *g = d->g;
	Gensio_Done *done = static_cast<Gensio_Done *>(user_data);

	try {
	    done->done(g);
	} catch (std::exception e) {
	    gensio_log(g->get_os_funcs(), GENSIO_LOG_ERR,
		       "Received C++ exception in close done handler: %s",
		       e.what());
	}
    }

    void Gensio::close(Gensio_Done *done)
    {
	int err;

	err = gensio_close(io, gensio_cpp_close_done, done);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::close_s()
    {
	int err = gensio_close_s(io);
	if (err)
	    throw gensio_error(err);
    }

    void Gensio::control(int depth, bool get, unsigned int option,
			 char *data, gensiods *datalen)
    {
	int err = gensio_control(io, depth, get, option, data, datalen);
	if (err)
	    throw gensio_error(err);
    }

    Gensio *Gensio::get_child(unsigned int depth)
    {
	struct gensio *io2 = gensio_get_child(io, depth);
	struct gensio_frdata *f;
	struct gensio_cpp_data *d;

	if (!io2)
	    return NULL;

	f = gensio_get_frdata(io2);
	d = gensio_container_of(f, struct gensio_cpp_data, frdata);
	return d->g;
    }

    int Gensio::read_s(gensiods *count, void *data, gensiods datalen,
		       gensio_time *timeout)
    {
	int err = gensio_read_s(io, count, data, datalen, timeout);
	if (err == GE_TIMEDOUT)
	    return err;
	if (err)
	    throw gensio_error(err);
	return 0;
    }

    int Gensio::read_s_intr(gensiods *count, void *data, gensiods datalen,
			     gensio_time *timeout)
    {
	int err = gensio_read_s_intr(io, count, data, datalen, timeout);
	if (err == GE_TIMEDOUT || err == GE_INTERRUPTED)
	    return err;
	if (err)
	    throw gensio_error(err);
	return 0;
    }

    Tcp::Tcp(struct gensio_addr *addr, const char * const args[],
	     struct gensio_os_funcs *o, Event *cb)
	: Gensio(o, cb)
    {
	struct gensio *io;
	int err;

	err = tcp_gensio_alloc(addr, args, o, NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	this->set_gensio(io);
    }

    Unix::Unix(struct gensio_addr *addr, const char * const args[],
	       struct gensio_os_funcs *o, Event *cb)
	: Gensio(o, cb)
    {
	struct gensio *io;
	int err;

	err = unix_gensio_alloc(addr, args, o, NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	this->set_gensio(io);
    }

    Stdio::Stdio(const char *const argv[], const char * const args[],
		 struct gensio_os_funcs *o, Event *cb)
	: Gensio(o, cb)
    {
	struct gensio *io;
	int err;

	err = stdio_gensio_alloc(argv, args, o, NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	this->set_gensio(io);
    }

    static void sergensio_cpp_done(struct sergensio *sio, int err,
			    unsigned int val, void *cb_data)
    {
	struct gensio *io = sergensio_to_gensio(sio);
	struct gensio_frdata *f = gensio_get_frdata(io);
	struct gensio_cpp_data *d = gensio_container_of(f,
					      struct gensio_cpp_data, frdata);
	Serial_Gensio *sg = (Serial_Gensio *) d->g;
	Serial_Op_Done *done = static_cast<Serial_Op_Done *>(cb_data);

	done->done(sg, err, val);
    }

    void Serial_Gensio::baud(unsigned int baud, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_baud(sio, baud, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::datasize(unsigned int size, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_datasize(sio, size, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::parity(unsigned int par, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_parity(sio, par, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::stopbits(unsigned int bits, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_stopbits(sio, bits, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::flowcontrol(unsigned int flow, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_flowcontrol(sio, flow, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::iflowcontrol(unsigned int flow,
					 Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_iflowcontrol(sio, flow, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::sbreak(unsigned int sbreak, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_sbreak(sio, sbreak, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::dtr(unsigned int dtr, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_dtr(sio, dtr, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::rts(unsigned int rts, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_rts(sio, rts, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::cts(unsigned int cts, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_cts(sio, cts, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::dcd_dsr(unsigned int dcd_dsr, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_dcd_dsr(sio, dcd_dsr, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    void Serial_Gensio::ri(unsigned int ri, Serial_Op_Done *done)
    {
	int err;
	sergensio_done donefunc = sergensio_cpp_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_ri(sio, ri, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    static void sergensio_cpp_sig_done(struct sergensio *sio, int err,
				       const char *sig, unsigned int len,
				       void *cb_data)
    {
	struct gensio *io = sergensio_to_gensio(sio);
	struct gensio_frdata *f = gensio_get_frdata(io);
	struct gensio_cpp_data *d = gensio_container_of(f,
					      struct gensio_cpp_data, frdata);
	Serial_Gensio *sg = (Serial_Gensio *) d->g;
	Serial_Op_Sig_Done *done = static_cast<Serial_Op_Sig_Done *>(cb_data);

	done->done(sg, err, sig, len);
    }

    void Serial_Gensio::signature(const char *sig, unsigned int len,
				      Serial_Op_Sig_Done *done)
    {
	int err;
	sergensio_done_sig donefunc = sergensio_cpp_sig_done;

	if (!done)
	    donefunc = NULL;
	err = sergensio_signature(sio, sig, len, donefunc, done);
	if (err)
	    throw gensio_error(err);
    }

    class Std_Ser_Op_Done: public Serial_Op_Done {
    public:
	Std_Ser_Op_Done(struct gensio_os_funcs *o) : waiter(o) { }

	void wait() { waiter.wait(1, NULL); }

	int err = 0;
	unsigned int val = 0;

    private:
	void done(Serial_Gensio *sf, int err, unsigned int val)
	{
	    this->err = err;
	    this->val = val;
	    waiter.wake();
	}
	Waiter waiter;
    };
    
    void Serial_Gensio::baud_s(unsigned int *baud)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->baud(*baud, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*baud = w.val;
    }

    void Serial_Gensio::datasize_s(unsigned int *size)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->datasize(*size, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*size = w.val;
    }

    void Serial_Gensio::parity_s(unsigned int *par)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->parity(*par, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*par = w.val;
    }

    void Serial_Gensio::stopbits_s(unsigned int *bits)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->stopbits(*bits, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*bits = w.val;
    }

    void Serial_Gensio::flowcontrol_s(unsigned int *flow)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->flowcontrol(*flow, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*flow = w.val;
    }

    void Serial_Gensio::iflowcontrol_s(unsigned int *flow)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->iflowcontrol(*flow, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*flow = w.val;
    }

    void Serial_Gensio::sbreak_s(unsigned int *sbreak)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->sbreak(*sbreak, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*sbreak = w.val;
    }

    void Serial_Gensio::dtr_s(unsigned int *dtr)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->dtr(*dtr, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*dtr = w.val;
    }

    void Serial_Gensio::rts_s(unsigned int *rts)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->rts(*rts, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*rts = w.val;
    }

    void Serial_Gensio::cts_s(unsigned int *cts)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->cts(*cts, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*cts = w.val;
    }

    void Serial_Gensio::dcd_dsr_s(unsigned int *dcd_dsr)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->dcd_dsr(*dcd_dsr, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*dcd_dsr = w.val;
    }

    void Serial_Gensio::ri_s(unsigned int *ri)
    {
	Std_Ser_Op_Done w(this->get_os_funcs());

	this->ri(*ri, &w);
	w.wait();
	if (w.err)
	    throw gensio_error(w.err);
	*ri = w.val;
    }
    
    void Serial_Gensio::modemstate(unsigned int state)
    {
	int err = sergensio_modemstate(sio, state);
	if (err)
	    throw gensio_error(err);
    }
    
    void Serial_Gensio::linestate(unsigned int state)
    {
	int err = sergensio_linestate(sio, state);
	if (err)
	    throw gensio_error(err);
    }
    
    void Serial_Gensio::flow_state(bool state)
    {
	int err = sergensio_flowcontrol_state(sio, state);
	if (err)
	    throw gensio_error(err);
    }
    
    Serialdev::Serialdev(const char *devname, const char * const args[],
			 struct gensio_os_funcs *o, Event *cb)
	: Serial_Gensio(o, cb)
    {
	struct gensio *io;
	int err;

	err = serialdev_gensio_alloc(devname, args, o, NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	this->set_gensio(io);
    }

    struct gensio_acc_cpp_data {
	struct gensio_acc_frdata frdata;
	Accepter *a;
    };

    static int gensio_acc_cpp_cb(struct gensio_accepter *acc, void *user_data,
				 int event, void *data)
    {
	Accepter *a = static_cast<Accepter *>(user_data);
	Accepter_Event *cb = a->get_cb();
	struct gensio *io;
	Gensio *g;

	try {
	    switch (event) {
	    case GENSIO_ACC_EVENT_NEW_CONNECTION:
		io = (struct gensio *) data;
		g = gensio_alloc(io, a->get_os_funcs(), NULL);
		cb->new_connection(a, g);
		break;

	    case GENSIO_ACC_EVENT_LOG: {
		struct gensio_loginfo *l = (struct gensio_loginfo *) data;
		cb->log(l->level, l->str, l->args);
		break;
	    }

	    case GENSIO_ACC_EVENT_PRECERT_VERIFY: {
		io = (struct gensio *) data;
		g = gensio_alloc(io, a->get_os_funcs());
		return cb->precert_verify(a, g);
	    }

	    case GENSIO_ACC_EVENT_AUTH_BEGIN: {
		io = (struct gensio *) data;
		g = gensio_alloc(io, a->get_os_funcs());
		return cb->auth_begin(a, g);
	    }

	    case GENSIO_ACC_EVENT_PASSWORD_VERIFY: {
		struct gensio_acc_password_verify_data *p =
		    (struct gensio_acc_password_verify_data *) data;

		g = gensio_alloc(io, a->get_os_funcs());
		return cb->password_verify(a, g, p->password, p->password_len);
	    }

	    case GENSIO_ACC_EVENT_REQUEST_PASSWORD: {
		struct gensio_acc_password_verify_data *p =
		    (struct gensio_acc_password_verify_data *) data;

		g = gensio_alloc(io, a->get_os_funcs());
		return cb->request_password(a, g,
						p->password, &p->password_len);
	    }

	    case GENSIO_ACC_EVENT_POSTCERT_VERIFY: {
		struct gensio_acc_postcert_verify_data *p =
		    (struct gensio_acc_postcert_verify_data *) data;

		g = gensio_alloc(io, a->get_os_funcs());
		return cb->postcert_verify(a, g, p->err, p->errstr);
	    }

	    default:
		return GE_NOTSUP;
	    }
	} catch (std::exception e) {
	    gensio_log(g->get_os_funcs(), GENSIO_LOG_ERR,
		     "Received C++ exception in accepter callback handler: %s",
		     e.what());
	    return GE_APPERR;
	}

        return 0;
    }

    void gensio_acc_cpp_freed(struct gensio_accepter *acc,
			      struct gensio_acc_frdata *frdata)
    {
	struct gensio_acc_cpp_data *d = gensio_container_of(frdata,
						 struct gensio_acc_cpp_data,
						 frdata);
	Accepter_Event *cb = d->a->get_cb();

	if (cb)
	    cb->freed();
	delete d->a;
	delete d;
    }

    Accepter *gensio_acc_alloc(struct gensio_accepter *acc,
			       struct gensio_os_funcs *o)
    {
	struct gensio_accepter *cacc;
	unsigned int i;
	struct gensio_acc_frdata *f;
	struct gensio_acc_cpp_data *d;
	Accepter *a;

	// Set frdata for the gensio and all children.
	for (i = 0; cacc = gensio_acc_get_child(acc, i); i++) {
	    if (gensio_acc_get_frdata(cacc))
		break; // It's already been set.
	    a = new Accepter(acc, o, NULL);
	    try {
		d = new struct gensio_acc_cpp_data;
	    } catch (...) {
		delete a;
		throw;
	    }
	    d->a = a;
	    d->frdata.freed = gensio_acc_cpp_freed;
	    gensio_acc_set_frdata(cacc, &d->frdata);
	}
	f = gensio_acc_get_frdata(acc);
	d = gensio_container_of(f, struct gensio_acc_cpp_data, frdata);
	d->a->acc = acc;
	return d->a;
    }

    Accepter *gensio_acc_alloc(std::string str, struct gensio_os_funcs *o,
			       Accepter_Event *cb)
    {
	struct gensio_accepter *acc;
	int err;
	Accepter *a;

	err = str_to_gensio_accepter(str.c_str(), o, NULL, NULL, &acc);
	if (err)
	    throw gensio_error(err);
	a = gensio_acc_alloc(acc, o);
	a->set_callback(cb);
	gensio_acc_set_callback(acc, gensio_acc_cpp_cb, a);
	return a;
    }

    Accepter *gensio_acc_alloc(Accepter *child, std::string str,
			       struct gensio_os_funcs *o,
			       Accepter_Event *cb)
    {
	struct gensio_accepter *acc;
	int err;
	Accepter *a;

	err = str_to_gensio_accepter_child(child->acc, str.c_str(), o,
					   NULL, NULL, &acc);
	if (err)
	    throw gensio_error(err);
	a = gensio_acc_alloc(acc, o);
	a->gcb = cb;
	gensio_acc_set_callback(acc, gensio_acc_cpp_cb, a);
	return a;
    }

    Accepter::Accepter(struct gensio_accepter *iacc, struct gensio_os_funcs *o,
		       Accepter_Event *cb)
    {
	acc = iacc;
	go = o;
	gcb = cb;
    }

    void Accepter::free()
    {
	gensio_acc_free(acc);
    }

    void Accepter::startup()
    {
	int err = gensio_acc_startup(acc);
	if (err)
	    throw gensio_error(err);
    }

    static void gensio_acc_cpp_done(struct gensio_accepter *acc,
				    void *user_data)
    {
	struct gensio_acc_frdata *f = gensio_acc_get_frdata(acc);
	struct gensio_acc_cpp_data *d = gensio_container_of(f,
					  struct gensio_acc_cpp_data, frdata);
	Accepter *a = d->a;
	Accepter_Done *done = static_cast<Accepter_Done *>(user_data);

	try {
	    done->done(a);
	} catch (std::exception e) {
	    gensio_log(a->get_os_funcs(), GENSIO_LOG_ERR,
		       "Received C++ exception in accepter done handler: %s",
		       e.what());
	}
    }

    void Accepter::shutdown(Accepter_Done *done)
    {
	int err;

	err = gensio_acc_shutdown(acc, gensio_acc_cpp_done, done);
	if (err)
	    throw gensio_error(err);
    }

    void Accepter::shutdown_s()
    {
	int err = gensio_acc_shutdown_s(acc);
	if (err)
	    throw gensio_error(err);
    }

    void Accepter::set_callback_enable_cb(bool enabled, Accepter_Done *done)
    {
	int err;

	err = gensio_acc_set_accept_callback_enable_cb(acc,
						       enabled,
						       gensio_acc_cpp_done,
						       done);
	if (err)
	    throw gensio_error(err);
    }

    void Accepter::set_callback_enable_s(bool enabled)
    {
	int err = gensio_acc_set_accept_callback_enable_s(acc, enabled);
	if (err)
	    throw gensio_error(err);
    }

    void Accepter::control(int depth, bool get, unsigned int option,
			   char *data, gensiods *datalen)
    {
	int err = gensio_acc_control(acc, depth, get, option, data, datalen);
	if (err)
	    throw gensio_error(err);
    }

    int Accepter::accept_s(gensio_time *timeout, Gensio **g)
    {
	struct gensio *io;
	int err = gensio_acc_accept_s(acc, timeout, &io);
	if (err == GE_TIMEDOUT)
	    return err;
	if (err)
	    throw gensio_error(err);
	*g = gensio_alloc(io, go, NULL);
	return 0;
    }

    int Accepter::accept_s_intr(gensio_time *timeout, Gensio **g)
    {
	struct gensio *io;
	int err = gensio_acc_accept_s_intr(acc, timeout, &io);
	if (err == GE_TIMEDOUT || err == GE_INTERRUPTED)
	    return err;
	if (err)
	    throw gensio_error(err);
	*g = gensio_alloc(io, go, NULL);
	return 0;
    }

    Gensio *Accepter::str_to_gensio(std::string str, Event *cb)
    {
	struct gensio *io;
	Gensio *g;
	int err = gensio_acc_str_to_gensio(acc, str.c_str(), NULL, NULL, &io);
	if (err)
	    throw gensio_error(err);
	g = gensio_alloc(io, go, cb);
	return g;
    }

    Accepter *Accepter::get_child(unsigned int depth)
    {
	struct gensio_accepter *acc2 = gensio_acc_get_child(acc, depth);
	struct gensio_acc_frdata *f;
	struct gensio_acc_cpp_data *d;

	if (!acc2)
	    return NULL;

	f = gensio_acc_get_frdata(acc2);
	d = gensio_container_of(f, struct gensio_acc_cpp_data, frdata);
	return d->a;
    }

    std::string Accepter::get_port()
    {
	char portbuf[100];
	gensiods len = sizeof(portbuf);

	portbuf[0] = '0';
	portbuf[1] = '\0';
	int err = gensio_acc_control(acc, GENSIO_CONTROL_DEPTH_FIRST,
				     true, GENSIO_ACC_CONTROL_LPORT,
				     portbuf, &len);
	if (err)
	    throw gensio_error(err);
	return std::string(portbuf, len);
    }

    Waiter::Waiter(struct gensio_os_funcs *o)
    {
	this->o = o;
	waiter = o->alloc_waiter(o);
	if (!waiter)
	    throw std::bad_alloc();
    }

    int
    Waiter::wait(unsigned int count, gensio_time *timeout) {
	int rv = o->wait(waiter, count, timeout);

	if (rv == GE_TIMEDOUT)
	    return rv;
	if (rv)
	    throw gensio_error(rv);
	return 0;
    }

    int
    Waiter::wait_intr(unsigned int count, gensio_time *timeout, void *sigmask) {
	int rv = o->wait_intr_sigmask(waiter, count, timeout, sigmask);

	if (rv == GE_TIMEDOUT || rv == GE_INTERRUPTED)
	    return rv;
	if (rv)
	    throw gensio_error(rv);
	return 0;
    }
}