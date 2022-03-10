server:	main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./log/log.h ./log/log.cpp ./log/block_queue.h
	g++ -o server main.cpp ./http/http_conn.cpp ./http/http_conn.h ./locker/locker.h ./threadpool/threadpool.h ./timer/timer.h ./timer/timer.cpp ./log/log.h ./log/log.cpp ./log/block_queue.h -pthread 

clean:
	rm -r server