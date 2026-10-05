CC ?= cc
CFLAGS ?= -O3 -std=c99 -Wall -Wextra -Wpedantic
HOSTCC ?= cc
HOSTCFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic
RV32I_CFLAGS ?= -O2 -std=c99 -Wall -Wextra -Wpedantic \
	-fno-stack-protector
RV32I_TARGET_CFLAGS ?= $(RV32I_CFLAGS) -ffreestanding
FRAMA_C ?= frama-c
CLANG_FORMAT := $(shell command -v clang-format-20 2>/dev/null || \
	command -v clang-format 2>/dev/null)
C_SOURCES := $(filter-out static_tables.h,$(wildcard *.c *.h))
TABLE_GENERATOR := generate_tables
RV32I_TABLES := static_tables_rv32i.inc
RV32I_REFERENCE := rv32i_reference
RV32I_TARGET_REFERENCE := rv32i_reference_target
RV32I_TARGET_TESTS := rv32i_reference_tests
RV32I_ASM_CORE := rv32i_solver_core.s
RV32I_ASM := rv32i_solver.s
RV32I_ASM_TEST := rv32i_solver_tests.s
RIPES ?= ./Ripes-v2.2.6-106-g5b8a616-linux-x86_64.AppImage
RIPES_RUN ?= APPIMAGE_EXTRACT_AND_RUN=1 $(RIPES)
RIPES_TIMEOUT ?= 120000
SAMPLE_STATE := 21345671111111
SAMPLE_SOLUTION := B' R' D2 R' B R B' R D2 B R'
VECTORS := tests/solutions.txt
# One per rejection path: short, long, cubie digit low, cubie digit high,
# orientation digit low, orientation digit high, non-digit, duplicate, parity.
INVALID_STATES := 1234567111111 123456711111111 02345671111111 82345671111111 \
	12345671111110 12345671111114 1234567111111a 11345671111111 12345671111112

.PHONY: all check check-ida check-h3 check-static check-rv32i-c \
	check-rv32i-asm prove clean indent

all: solver mini

solver: solver.c
	$(CC) $(CFLAGS) $< -o $@

mini: mini.c
	$(CC) $(CFLAGS) $< -o $@

my_solver: my_solver.c static_tables.h
	$(CC) $(CFLAGS) $< -o $@

$(RV32I_REFERENCE): rv32i_reference.c static_tables.h
	$(CC) $(RV32I_CFLAGS) $< -o $@

$(RV32I_TARGET_REFERENCE): rv32i_reference.c static_tables.h
	$(CC) $(RV32I_TARGET_CFLAGS) -DRV32I_TARGET $< -o $@

$(RV32I_TARGET_TESTS): rv32i_reference.c static_tables.h
	$(CC) $(RV32I_TARGET_CFLAGS) -DRV32I_TARGET_TESTS $< -o $@

$(TABLE_GENERATOR): generate_tables.c my_solver.c
	$(HOSTCC) $(HOSTCFLAGS) generate_tables.c -o $@

static_tables.h: $(TABLE_GENERATOR)
	./$(TABLE_GENERATOR) >$@.tmp
	mv $@.tmp $@

$(RV32I_TABLES): $(TABLE_GENERATOR)
	./$(TABLE_GENERATOR) --asm >$@.tmp
	mv $@.tmp $@

$(RV32I_ASM): $(RV32I_ASM_CORE) $(RV32I_TABLES) Makefile
	cat $(RV32I_TABLES) $(RV32I_ASM_CORE) >$@.tmp
	mv $@.tmp $@

$(RV32I_ASM_TEST): $(RV32I_ASM_CORE) $(RV32I_TABLES) Makefile
	cat $(RV32I_TABLES) >$@.tmp
	sed 's/\.word 0 # TEST_MODE/.word 1 # TEST_MODE/' \
		$(RV32I_ASM_CORE) >>$@.tmp
	mv $@.tmp $@

check-static: my_solver $(TABLE_GENERATOR) $(RV32I_TABLES)
	./$(TABLE_GENERATOR) | cmp - static_tables.h
	./$(TABLE_GENERATOR) --asm | cmp - $(RV32I_TABLES)
	./my_solver --self-test

check-ida: my_solver
	./my_solver --self-test

check-h3: my_solver
	./my_solver --full-test

check-rv32i-c: my_solver $(RV32I_REFERENCE) $(RV32I_TARGET_REFERENCE) \
		$(RV32I_TARGET_TESTS) $(VECTORS)
	./$(RV32I_TARGET_REFERENCE)
	./$(RV32I_TARGET_TESTS)
	@expected=$$(mktemp); actual=$$(mktemp); \
		trap 'rm -f "$$expected" "$$actual"' 0 1 2 15; \
		count=0; \
		while IFS='|' read -r state solution; do \
			case "$$state" in ""|\#*) continue ;; esac; \
			./my_solver "$$state" >"$$expected"; \
			./$(RV32I_REFERENCE) "$$state" >"$$actual"; \
			cmp -s "$$actual" "$$expected" || { \
				echo "RV32I C output mismatch for $$state"; exit 1; }; \
			count=$$((count + 1)); \
		done <$(VECTORS); \
		echo "$$count RV32I C solution vectors matched my_solver"
	@for bad in $(INVALID_STATES); do \
		./$(RV32I_REFERENCE) "$$bad" >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "RV32I C $$bad: expected status 2, got $$status"; \
			exit 1; \
		}; \
	done

check-rv32i-asm: $(RV32I_ASM_TEST)
	@set -e; \
	for processor in RV32_ISS RV32_5S; do \
		output=$$(mktemp); \
		trap 'rm -f "$$output"' 0 1 2 15; \
		if ! $(RIPES_RUN) --mode cli --src $(RV32I_ASM_TEST) -t asm \
			--proc $$processor --timeout $(RIPES_TIMEOUT) --iret --cycles \
			--regs --json >"$$output" 2>&1; then \
			cat "$$output"; \
			echo "$$processor: Ripes execution failed"; \
			exit 1; \
		fi; \
		grep -q '"x31": 1' "$$output" || { \
			cat "$$output"; \
			echo "$$processor: RV32I assembly self-test failed"; \
			exit 1; \
		}; \
		echo "$$processor: RV32I assembly self-test passed"; \
		grep -E 'instructions retired|"cycles"' "$$output"; \
		rm -f "$$output"; \
		trap - 0 1 2 15; \
	done

check: solver mini $(VECTORS)
	./solver --self-test
	@expected=$$(mktemp); actual=$$(mktemp); \
		trap 'rm -f "$$expected" "$$actual"' 0 1 2 15; \
		count=0; \
		while IFS='|' read -r state solution; do \
			case "$$state" in ""|\#*) continue ;; esac; \
			printf '%s\n' "$$solution" >"$$expected"; \
			for binary in ./solver ./mini; do \
				$$binary "$$state" >"$$actual"; \
				status=$$?; \
				test $$status -eq 0 || { \
					echo "$$binary $$state: exit status $$status"; exit 1; }; \
				cmp -s "$$actual" "$$expected" || { \
					echo "$$binary $$state: output mismatch"; \
					echo "  expected: $$solution"; \
					printf '  got:      '; cat "$$actual"; \
					echo "  ($$(wc -c <"$$expected") bytes expected, \
$$(wc -c <"$$actual") produced)"; exit 1; }; \
			done; \
			count=$$((count + 1)); \
		done <$(VECTORS); \
		echo "$$count solution vectors matched by solver and mini"
	@for binary in ./solver ./mini; do \
		for bad in $(INVALID_STATES); do \
			$$binary "$$bad" >/dev/null 2>&1; \
			status=$$?; \
			test $$status -eq 2 || { \
				echo "$$binary $$bad: expected status 2, got $$status"; exit 1; }; \
		done; \
		$$binary >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with no argument: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) $(SAMPLE_STATE) >/dev/null 2>&1; \
		status=$$?; \
		test $$status -eq 2 || { \
			echo "$$binary with two arguments: expected status 2, got $$status"; \
			exit 1; }; \
		$$binary $(SAMPLE_STATE) >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "$$binary with stdout closed: expected status 1, got $$status"; \
			exit 1; }; \
	done
	@./solver --self-test >&- 2>/dev/null; \
		status=$$?; \
		test $$status -eq 1 || { \
			echo "solver --self-test with stdout closed: expected 1, got $$status"; \
			exit 1; }
	@echo "invalid input rejected with status 2, unwritable stdout with status 1"

prove: solver.c
	@log=$$(mktemp); trap 'rm -f "$$log"' 0 1 2 15; \
		$(FRAMA_C) -wp -wp-fct quarter_turn,rank_state,valid,parse_state \
		-wp-rte -rte-verbose 0 -wp-prover alt-ergo -wp-timeout 20 \
		-wp-cache none solver.c >"$$log" 2>&1; rc=$$?; \
		grep -Fvx -e '[wp] Warning: Skipped RTE guards: unaligned pointers (\aligned not supported)' \
		-e '[wp] Warning: Skipped RTE guards: invalid function pointer calls (\valid_function not supported)' "$$log"; \
		test $$rc -eq 0 && awk '$$1 == "[wp]" && $$2 == "Proved" && $$3 == "goals:" && $$4 > 0 && $$4 == $$6 { ok = 1 } END { exit !ok }' "$$log" && \
		! grep -Eq '(^|[[:space:]])(Timeout|Unknown|Failed):' "$$log"

indent:
ifeq ($(CLANG_FORMAT),)
	$(error clang-format 20 not found)
endif
	@$(CLANG_FORMAT) --version | grep -q 'version 20' || \
		{ echo "error: clang-format version 20 required"; exit 1; }
	$(CLANG_FORMAT) -i $(C_SOURCES)

clean:
	$(RM) solver mini my_solver $(TABLE_GENERATOR) $(RV32I_REFERENCE) \
		$(RV32I_TARGET_REFERENCE) $(RV32I_TARGET_TESTS) \
		$(RV32I_ASM) $(RV32I_ASM_TEST) static_tables.h.tmp \
		$(RV32I_TABLES).tmp $(RV32I_ASM).tmp $(RV32I_ASM_TEST).tmp
