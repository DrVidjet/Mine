LIBS := -lraylib -lm -lGL -lpthread -ldl

# ------------------------- End of user editable code -------------------------
DASH    := "  +--------------------------+"
TARGET  := "  :     Raylib Makefile      :"
VERSION := "  :    Script: 2024.07.01    :"
AUTHOR  := "  :    By: Jan W. Zumwalt    :"

# set compiler
CC := g++

# additional header files
HDRS :=

# additional include files
INCS :=

# additional source files
SRCS := mine.cpp

# name of executable
EXEC := mine

# generate object file names
OBJS := $(SRCS:.cpp=.o)

# set compiler flags
CFLAGS :=  -ggdb3 -O0 $(LIBS) --std=c++17 -Wall

# default recipe
all: $(EXEC)

# recipe for building final executable
$(EXEC): $(OBJS) $(HDRS) $(INCS) Makefile
	$(CC) -o $@ $(OBJS) $(CFLAGS)
	@echo $(\n)
	@echo $(DASH)
	@echo $(TARGET)
	@echo $(VERSION)
	@echo $(AUTHOR)
	@echo $(DASH)
	@echo $(\n)

# recipe for building object files
  # $(OBJS): $(@:.o=.c) $(HDRS) Makefile
  # $(CC) -o $@ $(@:.o=.c) -c $(CFLAGS)

#$(OBJS): $(@:.o=.cpp) $(HDRS) Makefile
#	$(CC) -o $@ $(@:.o=.cpp) -c $(CFLAGS)

%.o: %.cpp
	$(CC) -c $< -o $@ $(CFLAGS)

# recipe to clean workspace
clean: 
	rm -f $(OBJS) $(EXEC)

run: $(EXEC)
	./$(EXEC)

.PHONY: all clean
