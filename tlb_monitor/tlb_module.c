#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/perf_event.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/mutex.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("OS Student");
MODULE_DESCRIPTION("TLB Miss Monitor for Educational Purposes");

#define PROC_NAME "tlb_monitor"

// Глобальные переменные нашего модуля
static struct perf_event *tlb_event = NULL;
static int target_pid = -1;
static DEFINE_MUTEX(tlb_mutex); // За щита от одновременной записи/чтения

// Функция ЧТЕНИЯ: что выдаст ядро, если сделать `cat /proc/tlb_monitor`
static int tlb_show(struct seq_file *m, void *v)
{
    u64 enabled, running, count = 0;

    mutex_lock(&tlb_mutex);
    if (tlb_event) {
        // Читаем значения счетчика в ядре
        count = perf_event_read_value(tlb_event, &enabled, &running);
    }
    mutex_unlock(&tlb_mutex);

    // Выводим данные в формате: "PID: 1234, Misses: 56789"
    seq_printf(m, "%d %llu\n", target_pid, count);
    return 0;
}

// Открытие файла на чтение (стандартная обертка seq_file)
static int tlb_open(struct inode *inode, struct file *file)
{
    return single_open(file, tlb_show, NULL);
}

// Функция ЗАПИСИ: что сделает ядро, если написать `echo 1234 > /proc/tlb_monitor`
static ssize_t tlb_write(struct file *file, const char __user *ubuf, size_t count, loff_t *ppos)
{
    char buf[16];
    int pid;
    struct task_struct *task;
    struct perf_event_attr attr;

    // Считываем PID от пользователя (избегая переполнения)
    if (count > sizeof(buf) - 1) count = sizeof(buf) - 1;
    if (copy_from_user(buf, ubuf, count)) return -EFAULT;
    buf[count] = '\0';

    // Парсим строку в int
    if (kstrtoint(strstrip(buf), 10, &pid)) return -EINVAL;

    mutex_lock(&tlb_mutex);

    // 1. Очищаем старый счетчик, если мы уже за кем-то следили
    if (tlb_event) {
        perf_event_release_kernel(tlb_event);
        tlb_event = NULL;
    }

    // 2. Ищем задачу (task_struct) по её PID
    task = get_pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        mutex_unlock(&tlb_mutex);
        pr_err("TLB_MONITOR: PID %d not found\n", pid);
        return -ESRCH;
    }

    // 3. Настраиваем счетчик DTLB Data Miss
    memset(&attr, 0, sizeof(attr));
    attr.type = PERF_TYPE_HW_CACHE;
    attr.size = sizeof(attr);
    attr.config = (PERF_COUNT_HW_CACHE_DTLB) |
                  (PERF_COUNT_HW_CACHE_OP_READ << 8) |
                  (PERF_COUNT_HW_CACHE_RESULT_MISS << 16);
    attr.disabled = 0; // Включен сразу
    attr.exclude_kernel = 0;
    attr.exclude_hv = 1;

    // 4. Подключаем perf_event к процессу
    tlb_event = perf_event_create_kernel_counter(&attr, -1, task, NULL, NULL);
    put_task_struct(task); // Уменьшаем счетчик ссылок, perf уже забрал себе контроль

    if (IS_ERR(tlb_event)) {
        pr_err("TLB_MONITOR: Failed to create perf event\n");
        tlb_event = NULL;
        target_pid = -1;
        mutex_unlock(&tlb_mutex);
        return -EINVAL;
    }

    target_pid = pid;
    mutex_unlock(&tlb_mutex);
    pr_info("TLB_MONITOR: Now tracking PID %d\n", pid);

    return count;
}

// Для ядер версии >= 5.6 используется proc_ops
static const struct proc_ops tlb_proc_ops = {
    .proc_open    = tlb_open,
    .proc_read    = seq_read,
    .proc_write   = tlb_write,
    .proc_lseek   = seq_lseek,
    .proc_release = single_release,
};

// Инициализация модуля (insmod)
static int __init tlb_module_init(void)
{
    proc_create(PROC_NAME, 0666, NULL, &tlb_proc_ops);
    pr_info("TLB_MONITOR: Module loaded. /proc/%s created\n", PROC_NAME);
    return 0;
}

// Выгрузка модуля (rmmod)
static void __exit tlb_module_exit(void)
{
    remove_proc_entry(PROC_NAME, NULL);
    
    // Подчищаем perf_event
    if (tlb_event) {
        perf_event_release_kernel(tlb_event);
    }
    pr_info("TLB_MONITOR: Module unloaded\n");
}

module_init(tlb_module_init);
module_exit(tlb_module_exit);
