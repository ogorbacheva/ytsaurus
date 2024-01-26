GO_LIBRARY()
IF (OS_DARWIN AND ARCH_ARM64)
    SRCS(
		alert.go
		auth.go
		cache.go
		cipher_suites.go
		common.go
		common_string.go
		conn.go
		handshake_client.go
		handshake_client_tls13.go
		handshake_messages.go
		handshake_server.go
		handshake_server_tls13.go
		key_agreement.go
		key_schedule.go
		notboring.go
		prf.go
		quic.go
		ticket.go
		tls.go
    )
ELSEIF (OS_DARWIN AND ARCH_X86_64)
    SRCS(
		alert.go
		auth.go
		cache.go
		cipher_suites.go
		common.go
		common_string.go
		conn.go
		handshake_client.go
		handshake_client_tls13.go
		handshake_messages.go
		handshake_server.go
		handshake_server_tls13.go
		key_agreement.go
		key_schedule.go
		notboring.go
		prf.go
		quic.go
		ticket.go
		tls.go
    )
ELSEIF (OS_LINUX AND ARCH_AARCH64)
    SRCS(
		alert.go
		auth.go
		cache.go
		cipher_suites.go
		common.go
		common_string.go
		conn.go
		handshake_client.go
		handshake_client_tls13.go
		handshake_messages.go
		handshake_server.go
		handshake_server_tls13.go
		key_agreement.go
		key_schedule.go
		notboring.go
		prf.go
		quic.go
		ticket.go
		tls.go
    )
ELSEIF (OS_LINUX AND ARCH_X86_64)
    SRCS(
		alert.go
		auth.go
		cache.go
		cipher_suites.go
		common.go
		common_string.go
		conn.go
		handshake_client.go
		handshake_client_tls13.go
		handshake_messages.go
		handshake_server.go
		handshake_server_tls13.go
		key_agreement.go
		key_schedule.go
		notboring.go
		prf.go
		quic.go
		ticket.go
		tls.go
    )
ELSEIF (OS_WINDOWS AND ARCH_X86_64)
    SRCS(
		alert.go
		auth.go
		cache.go
		cipher_suites.go
		common.go
		common_string.go
		conn.go
		handshake_client.go
		handshake_client_tls13.go
		handshake_messages.go
		handshake_server.go
		handshake_server_tls13.go
		key_agreement.go
		key_schedule.go
		notboring.go
		prf.go
		quic.go
		ticket.go
		tls.go
    )
ENDIF()
END()


RECURSE(
	fipsonly
)
