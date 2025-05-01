CC = clang
CFLAGS = -Wall -Werror -std=c11 -O3
LDFLAGS = -lm

OBJDIR = build
BINDIR = bin
SRCDIR = src
INCDIR = include

# Object files
OBJ = $(OBJDIR)/image.o $(OBJDIR)/main.o

# Targets
$(BINDIR)/main: $(OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -I$(INCDIR) -c -o $@ $<

clean:
	rm -rf $(OBJDIR) $(BINDIR)

# CC = clang
# CC_ARGS = -Wall -Werror -std=c23 -O3


# all: 
# 	$(CC) -c image.c $(CC_ARGS) 
# 	$(CC) main.c image.o -o main $(CC_ARGS) -lm 

# main: main.c image.o
# 	$(CC) $^ -o $@ $(CC_ARGS) -lm 

# image.o: image.c image.h
# 	$(CC) -c image.c $(CC_ARGS) 