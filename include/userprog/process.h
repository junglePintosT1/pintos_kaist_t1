#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"

struct aux_struct {
  struct file *file;              // 가상 주소와 매핑된 파일
  off_t offset;                  // 읽어야 할 파일 오프셋
  size_t read_bytes;              // 가상페이지에 쓰여 있는 데이터의 크기
  size_t zero_bytes;              // 0으로 채울 남은 페이지의 바이트
};

tid_t process_create_initd(const char *file_name);
tid_t process_fork(const char *name, struct intr_frame *if_);
int process_exec(void *f_name);
int process_wait(tid_t);
void process_exit(void);
void process_activate(struct thread *next);

/* NOTE: [2.4] 파일 디스크립터 관련 함수 원형 */
int process_add_file(struct file *f);
struct file *process_get_file(int fd);
void process_close_file(int fd);

#endif /* userprog/process.h */
