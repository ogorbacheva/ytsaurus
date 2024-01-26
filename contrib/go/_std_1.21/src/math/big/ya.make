GO_LIBRARY()
IF (OS_DARWIN AND ARCH_ARM64)
    SRCS(
		accuracy_string.go
		arith.go
		arith_arm64.s
		arith_decl.go
		decimal.go
		doc.go
		float.go
		floatconv.go
		floatmarsh.go
		ftoa.go
		int.go
		intconv.go
		intmarsh.go
		nat.go
		natconv.go
		natdiv.go
		prime.go
		rat.go
		ratconv.go
		ratmarsh.go
		roundingmode_string.go
		sqrt.go
    )
ELSEIF (OS_DARWIN AND ARCH_X86_64)
    SRCS(
		accuracy_string.go
		arith.go
		arith_amd64.go
		arith_amd64.s
		arith_decl.go
		decimal.go
		doc.go
		float.go
		floatconv.go
		floatmarsh.go
		ftoa.go
		int.go
		intconv.go
		intmarsh.go
		nat.go
		natconv.go
		natdiv.go
		prime.go
		rat.go
		ratconv.go
		ratmarsh.go
		roundingmode_string.go
		sqrt.go
    )
ELSEIF (OS_LINUX AND ARCH_AARCH64)
    SRCS(
		accuracy_string.go
		arith.go
		arith_arm64.s
		arith_decl.go
		decimal.go
		doc.go
		float.go
		floatconv.go
		floatmarsh.go
		ftoa.go
		int.go
		intconv.go
		intmarsh.go
		nat.go
		natconv.go
		natdiv.go
		prime.go
		rat.go
		ratconv.go
		ratmarsh.go
		roundingmode_string.go
		sqrt.go
    )
ELSEIF (OS_LINUX AND ARCH_X86_64)
    SRCS(
		accuracy_string.go
		arith.go
		arith_amd64.go
		arith_amd64.s
		arith_decl.go
		decimal.go
		doc.go
		float.go
		floatconv.go
		floatmarsh.go
		ftoa.go
		int.go
		intconv.go
		intmarsh.go
		nat.go
		natconv.go
		natdiv.go
		prime.go
		rat.go
		ratconv.go
		ratmarsh.go
		roundingmode_string.go
		sqrt.go
    )
ELSEIF (OS_WINDOWS AND ARCH_X86_64)
    SRCS(
		accuracy_string.go
		arith.go
		arith_amd64.go
		arith_amd64.s
		arith_decl.go
		decimal.go
		doc.go
		float.go
		floatconv.go
		floatmarsh.go
		ftoa.go
		int.go
		intconv.go
		intmarsh.go
		nat.go
		natconv.go
		natdiv.go
		prime.go
		rat.go
		ratconv.go
		ratmarsh.go
		roundingmode_string.go
		sqrt.go
    )
ENDIF()
END()
