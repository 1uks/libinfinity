SUBDIRS = util session cleanup certs
TESTS = inf-test-state-vector inf-test-chunk inf-test-text-session \
	inf-test-text-cleanup inf-test-text-fixline \
	inf-test-certificate-validate

AM_CPPFLAGS = \
	-I${top_srcdir} \
	${infinity_CFLAGS}

if WITH_INFGTK
AM_CPPFLAGS += \
	${infgtk_CFLAGS}
endif

if WITH_INFTEXTGTK
AM_CPPFLAGS += \
	${inftextgtk_CFLAGS}
endif

noinst_PROGRAMS = inf-test-tcp-connection inf-test-xmpp-connection \
	inf-test-tcp-server inf-test-xmpp-server inf-test-daemon \
	inf-test-browser inf-test-certificate-request inf-test-set-acl \
	inf-test-chat inf-test-state-vector inf-test-chunk \
	inf-test-text-operations inf-test-text-session \
	inf-test-text-cleanup inf-test-text-recover \
	inf-test-text-replay inf-test-reduce-replay inf-test-mass-join \
	inf-test-text-fixline inf-test-traffic-replay \
	inf-test-certificate-validate inf-test-text-quick-write

if WITH_INFTEXTGTK
noinst_PROGRAMS += inf-test-gtk-browser
endif

inf_test_tcp_connection_SOURCES = \
	inf-test-tcp-connection.c

inf_test_tcp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_xmpp_connection_SOURCES = \
	inf-test-xmpp-connection.c

inf_test_xmpp_connection_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_tcp_server_SOURCES = \
	inf-test-tcp-server.c

inf_test_tcp_server_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_xmpp_server_SOURCES = \
	inf-test-xmpp-server.c

inf_test_xmpp_server_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_daemon_SOURCES = \
	inf-test-daemon.c

inf_test_daemon_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_browser_SOURCES = \
	inf-test-browser.c

inf_test_browser_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_certificate_request_SOURCES = \
	inf-test-certificate-request.c

inf_test_certificate_request_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_set_acl_SOURCES = \
	inf-test-set-acl.c

inf_test_set_acl_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_chat_SOURCES = \
	inf-test-chat.c

inf_test_chat_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_state_vector_SOURCES = \
	inf-test-state-vector.c

inf_test_state_vector_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_chunk_SOURCES = \
	inf-test-chunk.c

inf_test_chunk_LDADD = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_operations_SOURCES = \
	inf-test-text-operations.c

inf_test_text_operations_LDADD = \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_session_SOURCES = \
	inf-test-text-session.c

inf_test_text_session_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_cleanup_SOURCES = \
	inf-test-text-cleanup.c

inf_test_text_cleanup_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_replay_SOURCES = \
	inf-test-text-replay.c

inf_test_text_replay_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_recover_SOURCES = \
	inf-test-text-recover.c

inf_test_text_recover_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_reduce_replay_SOURCES = \
	inf-test-reduce-replay.c

inf_test_reduce_replay_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_mass_join_SOURCES = \
	inf-test-mass-join.c

inf_test_mass_join_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

inf_test_text_fixline_SOURCES = \
	inf-test-text-fixline.c

inf_test_text_fixline_LDADD = \
	util/libinftestutil.a \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}

if WITH_INFTEXTGTK
inf_test_gtk_browser_SOURCES = \
	inf-test-gtk-browser.c

inf_test_gtk_browser_LDADD = \
	${top_builddir}/libinftextgtk/libinftextgtk-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfgtk/libinfgtk-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${inftextgtk_LIBS} ${infgtk_LIBS} ${inftext_LIBS} ${infinity_LIBS}
endif

inf_test_traffic_replay_SOURCES = \
	inf-test-traffic-replay.c

inf_test_traffic_replay_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_certificate_validate_SOURCES = \
	inf-test-certificate-validate.c

inf_test_certificate_validate_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${infinity_LIBS}

inf_test_text_quick_write_SOURCES = \
	inf-test-text-quick-write.c

inf_test_text_quick_write_LDADD = \
	${top_builddir}/libinfinity/libinfinity-$(LIBINFINITY_API_VERSION).la \
	${top_builddir}/libinftext/libinftext-$(LIBINFINITY_API_VERSION).la \
	${inftext_LIBS} ${infinity_LIBS}
