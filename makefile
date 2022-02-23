server:	main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp
	g++ -o server main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp -pthread 

clean:
	rm -r server