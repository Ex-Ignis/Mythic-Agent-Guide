# Build COFF modules — one .o per .c file.
# Upload the resulting .o to the agent with the "load <name>" command.
# "C:/Program Files/JetBrains/CLion 2025.3.1/bin/mingw/bin/mingw32-make.exe" \
#  CC="C:/Program Files/JetBrains/CLion 2025.3.1/bin/mingw/bin/gcc.exe" \
#  all check
# make all

CC = x86_64-w64-mingw32-gcc

# COFF flags:
#  -Os                              optimize for size
#  -fno-asynchronous-unwind-tables  no .eh_frame section — smaller and cleaner output
#  -fno-ident                       suppress the compiler version string (e.g. "GCC: (MinGW-W64) 13.1.0") — static signature
#  -fno-stack-protector             do NOT emit __stack_chk_fail (would be an unresolved external symbol)
#  -falign-functions=1              minimum function alignment — reduces size
#  -ffunction-sections              one section per function
#  -fdata-sections                  one section per data object
CFLAGS = -Os -fno-asynchronous-unwind-tables -fno-ident \
         -fno-stack-protector -fno-builtin -mno-stack-arg-probe \
         -falign-functions=1 \
         -ffunction-sections -fdata-sections \
         -I../Include

MODULES = ls.o

.PHONY: all clean check

all: $(MODULES)

# Pattern rule: build any .o from the .c of the same name
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f *.o

# Verify there are no unresolved external symbols (UND section should be empty)
check: all
	@echo "== Undefined symbols (should be empty) =="
	@objdump -t cd.o | grep UND || echo "[ok] (none)"
	@echo "== Symbol 'go' (must appear) =="
	@objdump -t cd.o | grep " go" || echo "[x] (not found)"
