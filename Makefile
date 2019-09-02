.PHONY: all clean run

all: main

main: src/main.c src/sched.c src/sched.h src/savectx64.h src/savectx64.s src/adjstack.c
	@echo "Building 'main'..."
	@gcc $^ -o $@

run: main
	./main

clean:
	@echo "Cleaning all built files..."
	rm -f *.o ./main

