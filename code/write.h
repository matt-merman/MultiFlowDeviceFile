#include "common.h"
#include "info.h"

int write(object_state *, const char *, loff_t *, size_t, session *, int);
void delayed_write(unsigned long);
long put_work(char *, size_t, loff_t *, session *, int);

typedef struct _packed_work
{
        void *buffer;
        struct work_struct the_work;
        const char *data;
        size_t len;
        loff_t *off;
        session *session;
        int minor;
} packed_work;

int write(object_state *the_object,
          const char *buff,
          loff_t *off,
          size_t len,
          session *session,
          int minor)
{

        memory_node *node, *current_node;
        char *buffer;
        int ret;
        wait_queue_head_t *wq;

        node = kzalloc(sizeof(memory_node), GFP_ATOMIC);
        buffer = kzalloc(len, GFP_ATOMIC);

        if (node == NULL || buffer == NULL)
        {
                AUDIT printk("%s: unable to allocate a memory\n", MODNAME);
                return -ENOMEM;
        }

        ret = copy_from_user(buffer, buff, len);

        // low priority write op. cannot fail 
        if (session->priority == LOW_PRIORITY){
                __sync_add_and_fetch (&lp_threads[minor], 1);
                mutex_lock(&(the_object->operation_synchronizer));
                __sync_add_and_fetch (&lp_threads[minor], -1);
                wq = &the_object->wq;
        }else{
                wq = get_lock(the_object, session, minor);
                if (wq == NULL)
                        return -EAGAIN;
        }

        if(session->priority == HIGH_PRIORITY) hp_bytes[minor] += len;
        else lp_bytes[minor] += len;

        AUDIT printk("%s: ALLOCATED a new memory node\n", MODNAME);
        AUDIT printk("%s: ALLOCATED %ld bytes\n", MODNAME, len);

        current_node = the_object->head;

        while (current_node->next != NULL)
                current_node = current_node->next;

        current_node->buffer = buffer;
        current_node->next = node;

        node->next = NULL;
        node->buffer = NULL;
 
        *off += (len - ret);

#ifndef TEST   
        mutex_unlock(&(the_object->operation_synchronizer));
        wake_up(wq);
#endif
        return ret;
}

void delayed_write(unsigned long data)
{
        session *session = container_of((void *)data, packed_work, the_work)->session;
        int minor = container_of((void *)data, packed_work, the_work)->minor;
        size_t len = container_of((void *)data, packed_work, the_work)->len;
        loff_t *off = container_of((void *)data, packed_work, the_work)->off;
        
        object_state *the_object = objects[minor];

        char *buff = kzalloc(len, GFP_ATOMIC); // non blocking memory allocation
        if (buff == NULL)
        {
                AUDIT printk("%s: tasklet buffer allocation failure\n", MODNAME);
                goto exit;
        }

        buff = (char *)container_of((void *)data, packed_work, the_work)->data;

        AUDIT printk("%s: this print comes from kworker daemon with PID=%d - running on CPU-core %d\n", MODNAME, current->pid, smp_processor_id());

        write(the_object, buff, off, len, session, minor);

        AUDIT printk("%s: releasing the task buffer at address %p - container of task is at %p\n", MODNAME, (void *)data, container_of((void *)data, packed_work, the_work));

        kfree(buff);

exit:

        kfree((void *)container_of((void *)data, packed_work, the_work));
        module_put(THIS_MODULE);
}

long put_work(char *buff,
              size_t len,
              loff_t *off,
              session *session,
              int minor)
{

        packed_work *the_task;

        if (!try_module_get(THIS_MODULE))
                return -ENODEV;

        AUDIT printk("%s: requested deferred work\n", MODNAME);

        the_task = kzalloc(sizeof(packed_work), GFP_ATOMIC); // non blocking memory allocation
        if (the_task == NULL)
        {
                AUDIT printk("%s: tasklet buffer allocation failure\n", MODNAME);
                module_put(THIS_MODULE);
                return -ENOMEM;
        }

        the_task->buffer = the_task;
        the_task->len = len;
        the_task->off = off;
        the_task->session = session;
        the_task->minor = minor;

        the_task->data = kzalloc(len, GFP_ATOMIC); // non blocking memory allocation
        if (the_task->data == NULL)
        {
                AUDIT printk("%s: tasklet buffer allocation failure\n", MODNAME);
                module_put(THIS_MODULE);
                return -ENOMEM;
        }

        strncpy((char *)the_task->data, buff, len);

        AUDIT printk("%s: work buffer allocation success - address is %p\n", MODNAME, the_task);

        __INIT_WORK(&(the_task->the_work), (void *)delayed_write, (unsigned long)(&(the_task->the_work)));

        schedule_work(&the_task->the_work);

        return 0;
}