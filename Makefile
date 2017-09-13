PROG = lm
SOURCES = main.c mongoose/mongoose.c mjs/mjs.c
CFLAGS += -Imongoose -Imjs

all: $(SOURCES)
	$(CC) $(SOURCES) -o $(PROG) $(CFLAGS)
