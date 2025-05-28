#include <stdio.h> 
#include <stdlib.h> 
#include <string.h>
#include <time.h>
#include <stdbool.h>

//─────────────────────────────────────────────────────────────────────────────
// 전체 시뮬레이션 상수 정의
//  - MAX_PROCESS_NUM   : 최대 프로세스 개수
//  - MAX_ARRIVAL       : 최대 도착 시간
//  - MAX_CPU_BURST     : 최대 CPU 버스트 길이
//  - MAX_IO_BURST      : 최대 I/O 버스트 길이
//  - MAX_PRIORITY      : 우선순위 수준 개수 (1~MAX_PRIORITY)
//  - MAX_TIME_QUANTUM  : Round-Robin 시 최대 타임 퀀텀
//  - MAX_IO_EVENTS     : 프로세스당 최대 I/O 요청 횟수
//  - MAX_GANTT_LENGTH  : Gantt 차트의 최대 표시 칸 수
//─────────────────────────────────────────────────────────────────────────────
#define MAX_PROCESS_NUM   3
#define MAX_ARRIVAL      20
#define MAX_CPU_BURST    20
#define MAX_IO_BURST      5
#define MAX_PRIORITY      7
#define MAX_TIME_QUANTUM  5
#define MAX_IO_EVENTS     3
#define MAX_GANTT_LENGTH  400

//─────────────────────────────────────────────────────────────────────────────
// 스케줄러 알고리즘 개수 및 이름, 평가 지표 배열
//  - SCHED_COUNT      : 스케줄러 종류 수
//  - sched_names[...] : 각 인덱스가 어떤 알고리즘인지 표시
//  - g_avg_wait[...]  : 각 알고리즘의 평균 대기 시간 (초기값 -1 = 미실행)
//  - g_avg_turn[...]  : 각 알고리즘의 평균 반환 시간
//─────────────────────────────────────────────────────────────────────────────
#define SCHED_COUNT 6
static const char *sched_names[SCHED_COUNT] = {
    "FCFS", "NP-SJF", "P-SJF", "NP-Priority", "P-Priority", "RR"
};

static float g_avg_wait[SCHED_COUNT] = { -1, -1, -1, -1, -1, -1 };
static float g_avg_turn[SCHED_COUNT] = { -1, -1, -1, -1, -1, -1 };

//-----------------------------------------------------------------------------
// 프로세스(Process) 구조체 정의
//
// 각 프로세스가 갖는 정보:
//  - pid            : 프로세스 식별자
//  - CPU_burst      : 총 CPU 사용량
//  - arrival        : 시스템 도착 시각
//  - priority       : 우선순위 (1이 가장 높은 우선순위)
//  - CPU_remaining  : 남은 CPU 사용 시간 (스케줄러 실행 중 감소)
//  - io_count       : 총 I/O 요청 횟수
//  - io_request_times[] : I/O 요청 시점 (남은 CPU가 이 값에 도달하면 I/O 발생)
//  - current_io     : 다음 I/O 요청 인덱스
//  - IO_burst       : I/O 작업 한 번당 소요 시간
//  - IO_remaining   : 남은 I/O 처리 시간 (I/O 중일 때 감소)
//  - waiting_time   : 실제 대기 시간 (평가용)
//  - turnaround_time: 반환 시간 = 완료 시각 - 도착 시각 (평가용)
//-----------------------------------------------------------------------------
typedef struct process {
    int pid;                                // 프로세스 번호
    int CPU_burst;                          // 할당된 전체 CPU 버스트
    int arrival;                            // 도착 시각
    int priority;                           // 우선순위

    int CPU_remaining;                      // 남은 CPU 버스트

    int io_count;                           // 총 I/O 이벤트 수
    int io_request_times[MAX_IO_EVENTS];    // CPU_remaining 이 이 값에 도달하면 I/O 요청
    int current_io;                         // 다음 I/O 이벤트 인덱스
    int IO_burst;                           // I/O 한 번에 걸리는 시간
    int IO_remaining;                       // 남은 I/O 처리 시간

    int waiting_time;                       // 대기 시간 (평가용)
    int turnaround_time;                    // 반환 시간 (평가용)
} process;

//------------------------------------------------------------------------------
// 2) Circular Queue (원형 큐) 구조체 및 관련 상수
//  - MAX_QUEUE_SIZE : 큐 용량 (프로세스 최대 수 + 1, full/empty 구분용)
//  - queue 구조체:
//      p[]    : 내부에 저장할 process 배열
//      front  : 다음 dequeue 위치 인덱스
//      rear   : 마지막 enqueue 위치 인덱스
//      size   : 현재 저장된 요소 개수
//------------------------------------------------------------------------------
#define MAX_QUEUE_SIZE  (MAX_PROCESS_NUM + 1)
typedef struct queue {
    process  p[MAX_QUEUE_SIZE];
    int      front, rear, size;
} queue;


//------------------------------------------------------------------------------
// 3) Gantt Chart 기록용 구조체
//  - chart[] : 각 타임슬롯별 실행된 PID ( 또는 -1 = idle )
//  - count   : 기록된 슬롯 수
//------------------------------------------------------------------------------
typedef struct gantt_chart {
    int chart[MAX_GANTT_LENGTH];
    int count;
} gantt_chart;


//------------------------------------------------------------------------------
// 4) 전역 완료 리스트 (evaluation 용)
//  - done[]      : 완료된 프로세스 정보를 저장
//  - done_count  : 완료된 프로세스 수
//------------------------------------------------------------------------------
process done[MAX_PROCESS_NUM];
int     done_count = 0;


//------------------------------------------------------------------------------
// 5) 큐 연산 함수 프로토타입
//  - create_queue()   : 빈 원형 큐 동적 생성 및 초기화
//  - enqueue(q, pr)  : 프로세스 pr을 큐 q에 삽입
//  - dequeue(q)      : 큐 q에서 front 요소 제거
//  - queue_front(q)  : 큐 q의 front 요소 포인터 반환
//  - queue_size(q)   : 큐 q에 저장된 요소 개수 반환
//  - queue_is_empty(q): 큐 q가 비어있는지 여부 반환
//------------------------------------------------------------------------------
queue*   create_queue(void);
void     enqueue(queue *q, process *pr);
void     dequeue(queue *q);
process* queue_front(queue *q);
int      queue_size(queue *q);
int      queue_is_empty(queue *q);


//------------------------------------------------------------------------------
// 6) 초기화 및 프로세스 생성 함수 프로토타입
//  - config(&rq, &wq, &jq, &gc) : ready, waiting, job 큐 및 gantt_chart 초기화
//  - create_process(jq)         : 랜덤 프로세스 생성 후 job 큐에 추가
//------------------------------------------------------------------------------
void config(queue **rq, queue **wq, queue **jq, gantt_chart **gc);
void create_process(queue *jq);


//------------------------------------------------------------------------------
// 7) I/O 처리 함수 프로토타입
//  - io_execute(wq, rq) : waiting 큐 wq의 I/O 작업 처리 후 ready 큐 rq로 복귀
//------------------------------------------------------------------------------
void io_execute(queue *wq, queue *rq);


//------------------------------------------------------------------------------
// 8) Gantt 차트 기록/출력 함수 프로토타입
//  - save_gantt(gc, pid)    : pid 실행 기록
//  - save_gantt_idle(gc)    : idle 기록
//  - print_gantt(gc)        : 누적된 Gantt 차트 출력
//------------------------------------------------------------------------------
void save_gantt(gantt_chart *gc, int pid);
void save_gantt_idle(gantt_chart *gc);
void print_gantt(gantt_chart *gc);


//------------------------------------------------------------------------------
// 9) 정렬 및 선택 유틸 함수 프로토타입
//  - sort_by_arrival(q)   : job 큐 q를 arrival 시간 기준 오름차순 정렬
//  - select_shortest(q)   : ready 큐 q에서 CPU_remaining 가장 짧은 프로세스 front로 이동
//  - select_highest(q)    : ready 큐 q에서 우선순위(숫자 작을수록) 가장 높은 프로세스 front로 이동
//------------------------------------------------------------------------------
void sort_by_arrival(queue *q);
void select_shortest(queue *q);
void select_highest(queue *q);

//------------------------------------------------------------------------------
// pick 콜백들
//  - pick_fcfs(queue *rq): FCFS 방식용, 별도 선택 로직 없이 큐 front 사용
//  - pick_sjf(queue *rq): SJF 방식용, select_shortest로 CPU_remaining이 가장 짧은 프로세스를 front로 이동
//  - pick_prio(queue *rq): Priority 방식용, select_highest로 우선순위(값 작을수록 높음)가 가장 높은 프로세스를 front로 이동
//------------------------------------------------------------------------------
void pick_fcfs(queue *rq) {
    // FCFS: ready 큐에서 아무 처리 없이 front가 다음 실행 대상
}

void pick_sjf(queue *rq) {
    // SJF: ready 큐에서 CPU_remaining 가장 짧은 프로세스를 front로 교체
    select_shortest(rq);
}

void pick_prio(queue *rq) {
    // Priority: ready 큐에서 우선순위가 가장 높은 프로세스를 front로 교체
    select_highest(rq);
}


//------------------------------------------------------------------------------
// 공통 스케줄러 루프: run_scheduler()
//  - jq          : 도착 대기(job) 큐
//  - rq          : 실행 대기(ready) 큐
//  - wq          : I/O 대기(waiting) 큐
//  - gc          : Gantt 차트 기록용 구조체
//  - pick_ready  : ready 큐에서 다음 실행 대상 선택 콜백
//  - preemptive  : 선점 여부 (true: 선점형, false: 비선점형)
//  - sched_idx   : 통계 저장용 알고리즘 인덱스
//
// 실행 순서:
//  1) job 큐를 arrival 순으로 정렬, I/O 인덱스 초기화
//  2) 루프: job, ready, waiting, 실행 중 프로세스 존재 시 계속
//     a) 현재 시각 도착 프로세스 → ready 큐로 이동
//     b) waiting 큐에서 I/O 완료된 프로세스 → ready 큐로 이동
//     c) 선점형이면 이전 exe를 ready 큐 뒤로 재삽입
//     d) CPU가 유휴라면:
//          - ready 큐 비어 있으면 idle 기록 후 시각++
//          - 아니면 pick_ready로 next exe 선택
//     e) 1 tick 실행:
//          - Gantt에 pid 기록
//          - I/O 요청 시점일 경우 I/O 처리 시작(이후 waiting 큐로 이동)
//          - 아니면 CPU_remaining--, I/O/arrival 재처리, 완료 시 통계 저장
//  3) 종료 후 평균 대기/턴어라운드 시간 계산 및 전역 배열에 저장
//------------------------------------------------------------------------------
void run_scheduler(queue *jq, queue *rq, queue *wq,
                   gantt_chart *gc,
                   void (*pick_ready)(queue*),
                   bool preemptive,
                   int sched_idx)
{
    int clock = 0;
    process *exe = NULL;
    done_count = 0;

    // 1) job 큐 arrival 정렬 및 I/O 이벤트 인덱스 초기화
    sort_by_arrival(jq);
    for (int i = 0; i < jq->size; i++) {
        jq->p[(jq->front + i) % MAX_QUEUE_SIZE].current_io = 0;
    }

    // 2) 시뮬레이션 루프
    while (jq->size || rq->size || wq->size || exe) {
        // 2a) 도착 프로세스 → ready 큐
        while (jq->size && jq->p[jq->front].arrival <= clock) {
            enqueue(rq, &jq->p[jq->front]);
            dequeue(jq);
        }
        // 2b) I/O 완료 프로세스 → ready 큐
        io_execute(wq, rq);

        // 2c) 선점형인 경우 실행 중 프로세스 재대기
        if (preemptive && exe) {
            enqueue(rq, exe);
            exe = NULL;
        }

        // 2d) CPU 할당: 유휴이면 idle 기록, 아니면 pick_ready 호출
        if (!exe) {
            if (!rq->size) {
                save_gantt_idle(gc);
                clock++;
                continue;
            }
            pick_ready(rq);
            exe = &rq->p[rq->front];
            dequeue(rq);
        }

        // 2e) 1 tick 실행
        save_gantt(gc, exe->pid);
        // I/O 요청 시점 체크
        if (exe->current_io < exe->io_count &&
            exe->CPU_remaining == exe->io_request_times[exe->current_io])
        {
            // I/O 직전 1 tick 실행 후 I/O 시작
            exe->CPU_remaining--;
            clock++;
            exe->IO_remaining = exe->IO_burst;
            exe->current_io++;
            enqueue(wq, exe);
            exe = NULL;
        } else {
            // 일반 CPU 1 tick
            exe->CPU_remaining--;
            clock++;
            // 각 tick마다 I/O/arrival 재처리
            io_execute(wq, rq);
            while (jq->size && jq->p[jq->front].arrival <= clock) {
                enqueue(rq, &jq->p[jq->front]);
                dequeue(jq);
            }
            // 완료 시 통계 기록
            if (exe->CPU_remaining == 0) {
                exe->turnaround_time = clock - exe->arrival;
                exe->waiting_time    = exe->turnaround_time
                                     - exe->CPU_burst
                                     - (exe->io_count * exe->IO_burst);
                done[done_count++]   = *exe;
                exe = NULL;
            }
        }
    }

    // 3) 평균 대기/턴어라운드 시간 계산
    double sw = 0, st = 0;
    for (int i = 0; i < done_count; i++) {
        sw += done[i].waiting_time;
        st += done[i].turnaround_time;
    }
    g_avg_wait[sched_idx] = sw / done_count;
    g_avg_turn[sched_idx] = st / done_count;
}

//-----------------------------------------------------------------------------
// 10) 평가 함수 프로토타입 및 RR 스케줄러 선언
//
// 함수 평가(evaluation):
//   - 모든 프로세스가 완료된 후 각 스케줄러별로 계산된 평균 대기 시간(g_avg_wait)
//     및 평균 반환 시간(g_avg_turn)을 화면에 출력합니다.
//
// Round Robin 스케줄러(scheduler_RR):
//   - RR만 고유 로직이므로 따로 분리되어 run_scheduler와 다르게 구현됩니다.
  
void evaluation(void);
void scheduler_RR(queue *rq, queue *wq, queue *jq, gantt_chart *gc);
  

//-----------------------------------------------------------------------------
// 11) main 함수
//
// 프로그램 시작점:
//   1. 난수 초기화(srand).
//   2. 세 개의 큐(orig_jq=원본 작업 큐, rq=ready 큐, wq=waiting 큐) 및
//      간트차트 객체(gc)를 준비(config).
//   3. 임의 프로세스를 orig_jq에 생성(create_process).
//   4. 사용자 선택에 따라 6가지 스케줄러(1~5: run_scheduler, 6: scheduler_RR)를 실행.
//      - 매 선택 시:
//        • orig_jq를 복사하여 jq(실행용 작업 큐) 복원.
//        • ready 큐와 waiting 큐의 front/rear/size를 초기화.
//        • 간트차트(count)와 완료 리스트(done_count)를 초기화.
//        • 스케줄러 실행 → Gantt 출력 → 평가 출력.
//   5. choice=0 입력 시 종료, 할당된 메모리 해제 후 return.
//
// 핵심 포인트:
//   - memcpy로 orig_jq 복사 → 매 스케줄러 간 독립적인 시뮬레이션.
//   - front/rear/size 직접 초기화로 큐를 "비우기".
//   - 일관된 화면 출력 흐름: Gantt → Evaluation.

int main(void) {
    srand((unsigned)time(NULL));

    queue *orig_jq, *rq, *wq;
    gantt_chart *gc;
    config(&rq, &wq, &orig_jq, &gc);
    create_process(orig_jq);

    int choice;
    do {
        printf("\nSelect Algorithm:\n"
               " 1) FCFS\n"
               " 2) NP-SJF\n"
               " 3) P-SJF\n"
               " 4) NP-Priority\n"
               " 5) P-Priority\n"
               " 6) Round Robin\n"
               " 0) Quit\n"
               "Choice> ");
        if (scanf("%d",&choice)!=1) break;
        if (choice==0) break;
        if (choice<1 || choice>6) {
            puts("Invalid choice");
            continue;
        }

        // 작업 큐 복원 및 준비
        queue *jq = create_queue();
        memcpy(jq, orig_jq, sizeof(queue));

        // ready/ waiting 큐 비우기
        rq->front = wq->front = 0;
        rq->rear  = wq->rear  = -1;
        rq->size  = wq->size  = 0;

        // 간트차트 및 완료 카운트 초기화
        gc->count     = 0;
        done_count    = 0;

        // 선택된 스케줄러 실행
        switch (choice) {
            case 1:
                run_scheduler(jq, rq, wq, gc, pick_fcfs, false, 0);
                break;
            case 2:
                run_scheduler(jq, rq, wq, gc, pick_sjf, false, 1);
                break;
            case 3:
                run_scheduler(jq, rq, wq, gc, pick_sjf, true, 2);
                break;
            case 4:
                run_scheduler(jq, rq, wq, gc, pick_prio, false, 3);
                break;
            case 5:
                run_scheduler(jq, rq, wq, gc, pick_prio, true, 4);
                break;
            case 6:
                scheduler_RR(rq, wq, jq, gc);
                break;
        }

        // 결과 출력
        print_gantt(gc);
        evaluation();
        free(jq);
    } while (1);

    // 동적 할당 메모리 해제
    free(rq); free(wq); free(orig_jq); free(gc);
    return 0;
}


//-----------------------------------------------------------------------------
// 12) 큐 연산 구현
//
// create_queue:
//   - 빈 원형 큐 구조체를 동적 할당하고 front=0, rear=-1, size=0 으로 초기화.
// enqueue:
//   - 큐가 가득 차지 않았다면 rear를 한 칸 옮긴 뒤 요소 추가, size++.
// dequeue:
//   - 큐가 비어있지 않다면 front를 한 칸 옮기고 size--.
// queue_front:
//   - front 위치의 프로세스 포인터 반환 (큐가 비어 있으면 NULL).
// queue_size, queue_is_empty:
//   - 현재 size 값 반환 또는 size==0 여부 반환.

queue* create_queue(void) {
    queue *q = malloc(sizeof(queue));
    if (!q) { perror("malloc"); exit(1); }
    q->front = 0;
    q->rear  = -1;
    q->size  = 0;
    return q;
}

void enqueue(queue *q, process *pr) {
    if (q->size >= MAX_PROCESS_NUM) return;         // 가득 찼으면 무시
    if (q->rear == MAX_QUEUE_SIZE-1) q->rear = -1;  // 배열 끝 → wrap-around
    q->p[++q->rear] = *pr;
    q->size++;
}

void dequeue(queue *q) {
    if (q->size == 0) return;
    q->front = (q->front + 1) % MAX_QUEUE_SIZE;
    q->size--;
}

process* queue_front(queue *q) {
    return q->size ? &q->p[q->front] : NULL;
}

int queue_size(queue *q) {
    return q->size;
}

int queue_is_empty(queue *q) {
    return q->size == 0;
}

//-----------------------------------------------------------------------------
// 초기화 및 프로세스 생성
//
// config:
//   - ready(rq), waiting(wq), job(jq) 큐와 Gantt 차트(gc)를 동적 할당하고
//     각 구조체를 기본 상태로 초기화합니다.
//   • rq, wq, jq: create_queue()로 크기, front, rear를 초기화
//   • gc: malloc으로 할당한 후 count를 0으로, chart 배열을 0으로 채워 처음 상태 설정
//
// create_process:
//   - 1~MAX_PROCESS_NUM 개의 프로세스를 랜덤 생성하여 job 큐(jq)에 넣습니다.
//   • pid, CPU_burst, arrival, priority 필드 초기화
//   • CPU_remaining ← CPU_burst 으로 남은 CPU 시간 설정
//   • io_count: 1~MAX_IO_EVENTS 개의 I/O 요청 횟수 결정
//   • io_request_times: CPU_remaining 이 해당 값이 되면 I/O로 전환될 시점을 랜덤 생성 후 오름차순 정렬
//   • current_io ← 0, IO_burst(랜덤), IO_remaining ← 0
//   • waiting_time, turnaround_time 초기화
//   • 정보를 화면에 출력하고 enqueue(jq, &tmp)로 작업 큐에 추가
//-----------------------------------------------------------------------------
void config(queue **rq, queue **wq, queue **jq, gantt_chart **gc){
    *rq = create_queue();
    *wq = create_queue();
    *jq = create_queue();
    *gc = malloc(sizeof(gantt_chart));
    (*gc)->count = 0;
    memset((*gc)->chart, 0, sizeof((*gc)->chart));
}

void create_process(queue *jq){
    int n = rand() % MAX_PROCESS_NUM + 1;
    printf("Generating %d processes\n", n);
    for (int i = 0; i < n; i++) {
        process tmp;
        tmp.pid           = i + 1;
        tmp.CPU_burst     = rand() % MAX_CPU_BURST + 1;
        tmp.arrival       = rand() % MAX_ARRIVAL;
        tmp.priority      = rand() % MAX_PRIORITY + 1;
        tmp.CPU_remaining = tmp.CPU_burst;

        // I/O 이벤트 시점 생성
        tmp.io_count = rand() % MAX_IO_EVENTS + 1;
        for (int k = 0; k < tmp.io_count; k++) {
            // CPU_remaining 이 이 값이 되면 I/O 요청
            tmp.io_request_times[k] = rand() % (tmp.CPU_burst - 1) + 1;
        }
        // I/O 요청 시점을 오름차순 정렬
        for (int a = 0; a < tmp.io_count - 1; a++) {
            for (int b = a + 1; b < tmp.io_count; b++) {
                if (tmp.io_request_times[a] > tmp.io_request_times[b]) {
                    int t = tmp.io_request_times[a];
                    tmp.io_request_times[a] = tmp.io_request_times[b];
                    tmp.io_request_times[b] = t;
                }
            }
        }

        tmp.current_io   = 0;
        tmp.IO_burst     = rand() % MAX_IO_BURST + 1;
        tmp.IO_remaining = 0;
        tmp.waiting_time    = 0;
        tmp.turnaround_time = 0;

        // 생성된 프로세스 정보 출력
        printf(" P%2d: CPU=%2d Arr=%2d Pri=%2d | IOcnt=%d times=",
               tmp.pid, tmp.CPU_burst, tmp.arrival, tmp.priority,
               tmp.io_count);
        for (int k = 0; k < tmp.io_count; k++) {
            printf("%d ", tmp.io_request_times[k]);
        }
        printf(" burst=%d\n", tmp.IO_burst);

        enqueue(jq, &tmp);
    }
}


//-----------------------------------------------------------------------------
// I/O 처리 함수
//
// io_execute:
//   - waiting 큐(wq)에 있는 프로세스 중 I/O_remaining--
//   • I/O_remaining > 0 → wq에 다시 enqueue(여전히 I/O 중)
//   • I/O_remaining == 0 → rq(ready 큐)로 이동하여 CPU 대기 상태로 복귀
//   - 매 tick마다 호출되어 I/O 큐를 순회하며 I/O 완료된 프로세스를 ready 큐로 재투입
//-----------------------------------------------------------------------------
void io_execute(queue *wq, queue *rq){
    int cnt = wq->size;
    while (cnt--) {
        process tmp = wq->p[wq->front];
        dequeue(wq);
        tmp.IO_remaining--;
        if (tmp.IO_remaining > 0) {
            enqueue(wq, &tmp);
        } else {
            enqueue(rq, &tmp);
        }
    }
}

//-----------------------------------------------------------------------------
// Gantt 차트 저장 및 출력 함수
//
// save_gantt:
//   - CPU에 프로세스가 실행된 한 타임 슬롯의 pid를 차트에 기록
// save_gantt_idle:
//   - CPU 유휴 상태(Idle)를 -1로 표시하여 차트에 기록
// print_gantt:
//   1) 차트의 연속된 동일 pid 구간을 묶어 “[P#]” 또는 “Idle” 막대로 출력
//   2) 각각의 막대가 끝나는 시점을 시간 축으로 정밀하게 표시
//   - gc->chart: 타임슬롯별 pid 배열
//   - gc->count: 기록된 총 타임슬롯 수
//-----------------------------------------------------------------------------
void save_gantt(gantt_chart *gc, int pid) {
    gc->chart[gc->count++] = pid;
}

void save_gantt_idle(gantt_chart *gc) {
    gc->chart[gc->count++] = -1;
}

void print_gantt(gantt_chart *gc) {
    printf("\n===== Gantt Chart =====\n\n");
    // 1) 막대(bar) 형태로 프로세스별 실행 구간 출력
    printf("|");
    int t = 0;
    for (int i = 0; i < gc->count; ) {
        int pid = gc->chart[i];
        int j = i + 1;
        // 연속된 동일 pid 구간을 찾는다
        while (j < gc->count && gc->chart[j] == pid) j++;
        int len = j - i;
        // 구간 레이블 출력: pid < 0 → Idle, 아니면 P#
        if (pid < 0)      printf(" Idle ");
        else              printf("  P%-2d ", pid);
        printf("|");
        i = j;
    }
    printf("\n");

    // 2) 시간 축(Time Line) 출력: 각 구간 끝나는 시점을 누적하여 표시
    printf("0");
    for (int i = 0; i < gc->count; ) {
        int pid = gc->chart[i];
        int j = i + 1;
        while (j < gc->count && gc->chart[j] == pid) j++;
        int len = j - i;
        t += len;
        // 7칸 너비로 끝나는 시각 정렬 출력
        printf("%7d", t);
        i = j;
    }
    printf("\n\n");
}


//-----------------------------------------------------------------------------
// 9) 정렬 및 선택 유틸리티 함수
//
// sort_by_arrival:
//   - arrival 필드를 기준으로 ready/job 큐를 안정적으로 버블 정렬
//   - O(n²) 알고리즘으로 소규모 프로세스 수에 적합
//
// select_shortest:
//   - CPU_remaining(남은 실행 시간)이 가장 작은 프로세스를 큐의 front로 교환
//   - Preemptive(PSJF) 및 Non-Preemptive SJF에서 ready 큐에서 호출
//
// select_highest:
//   - priority(숫자 작을수록 높음)가 가장 높은 프로세스를 큐의 front로 교환
//   - Preemptive 및 Non-Preemptive Priority에서 ready 큐에서 호출
//
// 각 함수는 queue *q의 내부 배열을 직접 교체하여 front 인덱스의 프로세스를 변경
//-----------------------------------------------------------------------------
void sort_by_arrival(queue *q) {
    for (int i = 0; i < q->size - 1; i++) {
        for (int j = 0; j < q->size - 1 - i; j++) {
            int a = (q->front + j) % MAX_QUEUE_SIZE;
            int b = (q->front + j + 1) % MAX_QUEUE_SIZE;
            if (q->p[a].arrival > q->p[b].arrival) {
                process tmp = q->p[a];
                q->p[a] = q->p[b];
                q->p[b] = tmp;
            }
        }
    }
}

void select_shortest(queue *q) {
    int best = q->front;
    for (int i = 1; i < q->size; i++) {
        int idx = (q->front + i) % MAX_QUEUE_SIZE;
        if (q->p[idx].CPU_remaining < q->p[best].CPU_remaining) {
            best = idx;
        }
    }
    if (best != q->front) {
        process tmp = q->p[best];
        q->p[best] = q->p[q->front];
        q->p[q->front] = tmp;
    }
}

void select_highest(queue *q) {
    int best = q->front;
    for (int i = 1; i < q->size; i++) {
        int idx = (q->front + i) % MAX_QUEUE_SIZE;
        if (q->p[idx].priority < q->p[best].priority) {
            best = idx;
        }
    }
    if (best != q->front) {
        process tmp = q->p[best];
        q->p[best] = q->p[q->front];
        q->p[q->front] = tmp;
    }
}

//-----------------------------------------------------------------------------
// 10) Evaluation: 스케줄링 알고리즘별 결과 요약 출력
//
// - 각 알고리즘 이름, 평균 대기 시간, 평균 반환 시간을 표 형태로 출력합니다.
// - g_avg_wait[i], g_avg_turn[i]에 -1.0f가 들어있으면 해당 알고리즘이 아직 실행되지 않은 상태이므로 "Null"로 표시합니다.
// - SCHED_COUNT는 알고리즘 수, sched_names[]는 알고리즘 이름 배열입니다.
void evaluation() {
    printf("\n===== Scheduler Comparison =====\n");
    printf("%-12s | %-12s | %-12s\n", "Algorithm", "Avg Waiting", "Avg Turnaround");
    printf("-------------+--------------+--------------\n");
    for (int i = 0; i < SCHED_COUNT; i++) {
        if (g_avg_wait[i] != -1.0f) {
            // 이미 실행된 알고리즘의 평균값을 소수점 둘째 자리까지 출력
            printf("%-12s | %12.2f | %12.2f\n",
                   sched_names[i], g_avg_wait[i], g_avg_turn[i]);
        } else {
            // 실행되지 않은 알고리즘은 Null로 표시
            printf("%-12s | %12s | %12s\n",
                   sched_names[i], "Null", "Null");
        }
    }
    printf("\n");
}


//-----------------------------------------------------------------------------
// 11) Round Robin (RR): 시분할(Time Quantum) 스케줄링 알고리즘
//
// - 준비 큐(rq)에서 맨 앞 프로세스를 1틱씩 실행하되, 최대 MAX_TIME_QUANTUM 틱까지만 실행.
// - I/O 요청 시점(이전 틱에 io_request_times[current_io] 도달) 전 1틱 실행 후 waiting 큐(wq)로 이동.
// - waiting 큐는 매 틱마다 io_execute()로 I/O 버스트를 감소시키고, 완료된 프로세스를 다시 ready 큐로 이동.
// - CPU가 idle 상태일 때는 Gantt 차트에 Idle 기록 후 clock 증가.
// - 프로세스가 종료되면 turnaround_time과 waiting_time을 계산해 done[]에 저장.
// - 모든 프로세스가 완료되면 평균 대기/반환 시간을 계산해 전역 배열에 저장.
//
// 파라미터:
//   rq - 준비 큐 (ready queue)
//   wq - 대기 큐 (waiting queue) for I/O
//   jq - 잡 큐 (job queue) for 아직 도착하지 않은 프로세스
//   gc - Gantt 차트 저장용 구조체
//-----------------------------------------------------------------------------
void scheduler_RR(queue *rq, queue *wq, queue *jq, gantt_chart *gc) {
    int clock = 0;
    process *exe = NULL;

    // 1) 도착 순서로 job 큐 정렬
    sort_by_arrival(jq);
    // 2) 각 프로세스의 I/O 이벤트 인덱스 초기화
    for (int i = 0; i < jq->size; i++) {
        jq->p[(jq->front + i) % MAX_QUEUE_SIZE].current_io = 0;
    }

    // 3) 메인 루프: job, ready, waiting, or 실행 중인 프로세스가 남아 있는 동안 반복
    while (jq->size || rq->size || wq->size || exe) {
        // 3-1) 시점 clock에 새로 도착한 프로세스 → ready 큐로 이동
        while (jq->size && jq->p[jq->front].arrival <= clock) {
            enqueue(rq, &jq->p[jq->front]);
            dequeue(jq);
        }
        // 3-2) waiting 큐에서 I/O 완료된 프로세스 → ready 큐로 이동
        io_execute(wq, rq);

        // 3-3) CPU가 비어 있으면 ready 큐에서 꺼내거나, 비어 있으면 Idle
        if (!exe) {
            if (!rq->size) {
                save_gantt_idle(gc);
                clock++;
                continue;
            }
            exe = queue_front(rq);
            dequeue(rq);
        }

        // 3-4) 할당된 Time Quantum만큼(최대 MAX_TIME_QUANTUM 틱) 실행
        for (int t = 0; t < MAX_TIME_QUANTUM && exe; t++) {
            // I/O 요청 시점 체크
            if (exe->current_io < exe->io_count &&
                exe->CPU_remaining == exe->io_request_times[exe->current_io]) {
                // I/O 요청 직전 1틱 실행
                save_gantt(gc, exe->pid);
                exe->CPU_remaining--;
                clock++;
                // I/O 버스트 시작 → waiting 큐로 이동
                exe->IO_remaining = exe->IO_burst;
                exe->current_io++;
                enqueue(wq, exe);
                exe = NULL;
            }
            else {
                // 일반 CPU 1틱 실행
                save_gantt(gc, exe->pid);
                exe->CPU_remaining--;
                clock++;

                // 매 틱마다 I/O 및 도착 프로세스 처리
                io_execute(wq, rq);
                while (jq->size && jq->p[jq->front].arrival <= clock) {
                    enqueue(rq, &jq->p[jq->front]);
                    dequeue(jq);
                }

                // 프로세스 완료 시 turnaround/wait 계산 후 done[] 저장
                if (exe->CPU_remaining == 0) {
                    exe->turnaround_time = clock - exe->arrival;
                    exe->waiting_time    = exe->turnaround_time
                                         - exe->CPU_burst
                                         - (exe->io_count * exe->IO_burst);
                    done[done_count++]   = *exe;
                    exe = NULL;
                }
                // Quantum 만료 시 ready 큐로 다시 삽입
                else if (t == MAX_TIME_QUANTUM - 1) {
                    enqueue(rq, exe);
                    exe = NULL;
                }
            }
        }
    }

    // 4) RR 실행 후 평균 대기/반환 시간 계산
    {
        double sum_w = 0.0, sum_t = 0.0;
        for (int i = 0; i < done_count; i++) {
            sum_w += done[i].waiting_time;
            sum_t += done[i].turnaround_time;
        }
        g_avg_wait[5] = sum_w / done_count;
        g_avg_turn[5] = sum_t / done_count;
    }
}
