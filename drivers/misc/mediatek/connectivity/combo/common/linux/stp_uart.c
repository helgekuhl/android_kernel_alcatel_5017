#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <linux/spinlock.h>

#include <linux/time.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kfifo.h>


#include "stp_exp.h"

#define N_MTKSTP              (15 + 1)	/* refer to linux tty.h use N_HCI. */

#define HCIUARTSETPROTO        _IOW('U', 200, int)

#define MAX(a, b)        ((a) > (b) ? (a) : (b))
#define MIN(a, b)        ((a) < (b) ? (a) : (b))

#define PFX                         "[UART] "
#define UART_LOG_LOUD                 4
#define UART_LOG_DBG                  3
#define UART_LOG_INFO                 2
#define UART_LOG_WARN                 1
#define UART_LOG_ERR                  0

#define MAX_PACKET_ALLOWED                2000


static UINT32 gDbgLevel = UART_LOG_INFO;


#define UART_DBG_FUNC(fmt, arg...)	\
do { if (gDbgLevel >= UART_LOG_DBG)	\
		pr_info(PFX "%s: "  fmt, __func__ , ##arg);	\
} while (0)
#define UART_INFO_FUNC(fmt, arg...)	\
do { if (gDbgLevel >= UART_LOG_INFO)	\
		pr_info(PFX "%s: "  fmt, __func__ , ##arg);	\
} while (0)
#define UART_WARN_FUNC(fmt, arg...)	\
do { if (gDbgLevel >= UART_LOG_WARN)	\
		pr_warn(PFX "%s: "  fmt, __func__ , ##arg);	\
} while (0)
#define UART_ERR_FUNC(fmt, arg...)	\
do { if (gDbgLevel >= UART_LOG_ERR)	\
		pr_err(PFX "%s: "   fmt, __func__ , ##arg);	\
} while (0)
#define UART_TRC_FUNC(f)	\
do { if (gDbgLevel >= UART_LOG_DBG)	\
		pr_debug(PFX "<%s> <%d>\n", __func__, __LINE__);	\
} while (0)


#include <linux/kfifo.h>
#define LDISC_RX_TASKLET  0
#define LDISC_RX_WORK  1

#if WMT_UART_RX_MODE_WORK
#define LDISC_RX LDISC_RX_WORK
#else
#define LDISC_RX LDISC_RX_TASKLET
#endif

#if (LDISC_RX == LDISC_RX_TASKLET)
#define LDISC_RX_FIFO_SIZE (0x20000)	/*8192 bytes */
struct kfifo *g_stp_uart_rx_fifo = NULL;
spinlock_t g_stp_uart_rx_fifo_spinlock;
struct tasklet_struct g_stp_uart_rx_fifo_tasklet;
#define RX_BUFFER_LEN 1024
UINT8 g_rx_data[RX_BUFFER_LEN];

/* static DEFINE_RWLOCK(g_stp_uart_rx_handling_lock); */
#elif (LDISC_RX == LDISC_RX_WORK)

#define LDISC_RX_FIFO_SIZE (0x4000)	/* 16K bytes shall be enough...... */
#define LDISC_RX_BUF_SIZE (2048)	/* 2K bytes in one shot is enough */

PUINT8 g_stp_uart_rx_buf;	/* for stp rx data parsing */
struct kfifo *g_stp_uart_rx_fifo = NULL;	/* for uart tty data receiving */
spinlock_t g_stp_uart_rx_fifo_spinlock;	/* fifo spinlock */
struct workqueue_struct *g_stp_uart_rx_wq;	/* rx work queue (do not use system_wq) */
struct work_struct *g_stp_uart_rx_work;	/* rx work */

#endif

struct tty_struct *stp_tty = 0x0;

UINT8 tx_buf[MTKSTP_BUFFER_SIZE] = { 0x0 };

INT32 rd_idx = 0;
INT32 wr_idx = 0;
/* struct semaphore buf_mtx; */
spinlock_t buf_lock;
static INT32 mtk_wcn_uart_tx(const PUINT8 data, const UINT32 size, PUINT32 written_size);


static inline INT32 stp_uart_tx_wakeup(struct tty_struct *tty)
{
	INT32 len = 0;
	INT32 written = 0;
	INT32 written_count = 0;
	static INT32 i;
	/* UINT32 flags; */
	/* get data from ring buffer */
/* down(&buf_mtx); */
	UART_DBG_FUNC("++\n");
/* //    spin_lock_irqsave(&buf_lock, flags); */

#if 0
	if ((i > 1000) && (i % 5) == 0) {
		UART_INFO_FUNC("i=(%d), ****** drop data from uart******\n", i);
		i++;
		return 0;
	} else {

		UART_INFO_FUNC("i=(%d)at stp uart **\n", i);
	}
#endif

	len = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (MTKSTP_BUFFER_SIZE - rd_idx);
	if (len > 0 && len < MAX_PACKET_ALLOWED) {
		i++;
		/*
		 *     ops->write is called by the kernel to write a series of
		 *     characters to the tty device.  The characters may come from
		 *     user space or kernel space.  This routine will return the
		 *    number of characters actually accepted for writing.
		 */
		set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
		written = tty->ops->write(tty, &tx_buf[rd_idx], len);
		if (written != len) {
			UART_ERR_FUNC
			    ("Error(i-%d):[pid(%d)(%s)]tty-ops->write FAIL!len(%d)wr(%d)wr_i(%d)rd_i(%d)\n\r",
			     i, current->pid, current->comm, len, written, wr_idx, rd_idx);
			return -1;
		}
		written_count = written;
		/* pr_warn("len = %d, written = %d\n", len, written); */
		rd_idx = ((rd_idx + written) % MTKSTP_BUFFER_SIZE);
		/* all data is accepted by UART driver, check again in case roll over */
		len = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (MTKSTP_BUFFER_SIZE - rd_idx);
		if (len > 0 && len < MAX_PACKET_ALLOWED) {
			set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
			written = tty->ops->write(tty, &tx_buf[rd_idx], len);
			if (written != len) {
				UART_ERR_FUNC
				    ("Error(i-%d):[pid(%d)(%s)]len(%d)wr(%d)wr_i(%d)rd_i(%d)\n\r",
				     i, current->pid, current->comm, len, written, wr_idx, rd_idx);
				return -1;
			}
			rd_idx = ((rd_idx + written) % MTKSTP_BUFFER_SIZE);
			written_count += written;
		} else if (len < 0 || len >= MAX_PACKET_ALLOWED) {
			UART_ERR_FUNC
			    ("Warnning(i-%d):[pid(%d)(%s)]length verfication(external) warnning,len(%d), wr_idx(%d), rd_idx(%d)!\n\r",
			     i, current->pid, current->comm, len, wr_idx, rd_idx);
			return -1;
		}
	} else {
		UART_ERR_FUNC
		    ("Warnning(i-%d):[pid(%d)(%s)]length verfication(external) warnning,len(%d), wr_idx(%d), rd_idx(%d)!\n\r",
		     i, current->pid, current->comm, len, wr_idx, rd_idx);
		return -1;
	}
	/* up(&buf_mtx); */
/* //    spin_unlock_irqrestore(&buf_lock, flags); */
	UART_DBG_FUNC("--\n");
	return written_count;
}

/* ------ LDISC part ------ */
static int stp_uart_tty_open(struct tty_struct *tty)
{
	UART_DBG_FUNC("stp_uart_tty_opentty: %p\n", tty);

	tty->receive_room = 65536;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
	tty->port->low_latency = 1;
#else
	tty->low_latency = 1;
#endif

	/* Flush any pending characters in the driver and line discipline. */

	/* FIXME: why is this needed. Note don't use ldisc_ref here as the
	   open path is before the ldisc is referencable */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2, 6, 29)
	/* definition changed!! */
	if (tty->ldisc->ops->flush_buffer) {
		tty->ldisc->ops->flush_buffer(tty);
	}
#else
	if (tty->ldisc.ops->flush_buffer) {
		tty->ldisc.ops->flush_buffer(tty);
	}
#endif

	tty_driver_flush_buffer(tty);

/* init_MUTEX(&buf_mtx); */
/* //    spin_lock_init(&buf_lock); */

	rd_idx = wr_idx = 0;
	stp_tty = tty;
	mtk_wcn_stp_register_if_tx(STP_UART_IF_TX, (MTK_WCN_STP_IF_TX)mtk_wcn_uart_tx);

	return 0;
}

static void stp_uart_tty_close(struct tty_struct *tty)
{
	UART_DBG_FUNC("stp_uart_tty_close(): tty %p\n", tty);
	mtk_wcn_stp_register_if_tx(STP_UART_IF_TX, NULL);

	return;
}

static void stp_uart_tty_wakeup(struct tty_struct *tty)
{
	/* pr_warn("%s: start !!\n", __FUNCTION__); */

	/* clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags); */

	/* stp_uart_tx_wakeup(tty); */

	return;
}

#if  (LDISC_RX  == LDISC_RX_TASKLET)

static INT32 stp_uart_fifo_init(VOID)
{
	INT32 err = 0;
	/*add rx fifo */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
	{
		spin_lock_init(&g_stp_uart_rx_fifo_spinlock);
		g_stp_uart_rx_fifo =
		    kfifo_alloc(LDISC_RX_FIFO_SIZE, GFP_ATOMIC, &g_stp_uart_rx_fifo_spinlock);
		if (NULL == g_stp_uart_rx_fifo) {
			UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.35)\n");
			err = -1;
		}
	}
#else
	{
		g_stp_uart_rx_fifo = kzalloc(sizeof(struct kfifo), GFP_ATOMIC);
		if (NULL == g_stp_uart_rx_fifo) {
			err = -2;
			UART_ERR_FUNC
			    ("kzalloc for g_stp_uart_rx_fifo failed (kernel version > 2.6.35)\n");
		}
		err = kfifo_alloc(g_stp_uart_rx_fifo, LDISC_RX_FIFO_SIZE, GFP_ATOMIC);
		if (0 != err) {
			UART_ERR_FUNC("kfifo_alloc failed, errno(%d)(kernel version > 2.6.35)\n",
				      err);
			kfree(g_stp_uart_rx_fifo);
			g_stp_uart_rx_fifo = NULL;
			err = -3;
		}
	}
#endif
	if (0 == err) {
		if (NULL != g_stp_uart_rx_fifo) {
			kfifo_reset(g_stp_uart_rx_fifo);
			UART_ERR_FUNC("stp_uart_fifo_init() success.\n");
		} else {
			err = -4;
			UART_ERR_FUNC
			    ("abnormal case, err = 0 but g_stp_uart_rx_fifo = NULL, set err to %d\n",
			     err);
		}
	} else {
		UART_ERR_FUNC("stp_uart_fifo_init() failed.\n");
	}
	return err;
}

static INT32 stp_uart_fifo_deinit(VOID)
{
	if (NULL != g_stp_uart_rx_fifo) {
		kfifo_free(g_stp_uart_rx_fifo);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
		/* do nothing */
#else
		kfree(g_stp_uart_rx_fifo);
#endif
		g_stp_uart_rx_fifo = NULL;
	}
	return 0;
}

static void stp_uart_rx_handling(unsigned long func_data)
{
	UINT32 how_much_get = 0;
	UINT32 how_much_to_get = 0;
	UINT32 flag = 0;

/* read_lock(&g_stp_uart_rx_handling_lock); */
	how_much_to_get = kfifo_len(g_stp_uart_rx_fifo);

	if (how_much_to_get >= RX_BUFFER_LEN) {
		flag = 1;
		UART_INFO_FUNC("fifolen(%d)\n", how_much_to_get);
	}

	do {
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
		how_much_get = kfifo_get(g_stp_uart_rx_fifo, g_rx_data, RX_BUFFER_LEN);
#else
		how_much_get = kfifo_out(g_stp_uart_rx_fifo, g_rx_data, RX_BUFFER_LEN);
#endif
		/* UART_INFO_FUNC ("fifoget(%d)\n", how_much_get); */
		mtk_wcn_stp_parser_data((UINT8 *) g_rx_data, how_much_get);
		how_much_to_get = kfifo_len(g_stp_uart_rx_fifo);
	} while (how_much_to_get > 0);

/* read_unlock(&g_stp_uart_rx_handling_lock); */
	if (1 == flag) {
		UART_INFO_FUNC("finish, fifolen(%d)\n", kfifo_len(g_stp_uart_rx_fifo));
	}
}

static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{
	UINT32 fifo_avail_len = LDISC_RX_FIFO_SIZE - kfifo_len(g_stp_uart_rx_fifo);
	UINT32 how_much_put = 0;
#if 0
	{
		struct timeval now;
		do_gettimeofday(&now);
		pr_warn("[+STP][  ][R] %4d --> sec = %lu, --> usec --> %lu\n",
			count, now.tv_sec, now.tv_usec);
	}
#endif
/* write_lock(&g_stp_uart_rx_handling_lock); */
	if (count > 2000) {
		/*this is abnormal */
		UART_ERR_FUNC("abnormal: buffer count = %d\n", count);
	}
	/*How much empty seat? */
	if (fifo_avail_len > 0) {
		/* UART_INFO_FUNC ("fifo left(%d), count(%d)\n", fifo_avail_len, count); */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35))
		how_much_put = kfifo_put(g_stp_uart_rx_fifo, (PUINT8)data, count);
#else
		how_much_put = kfifo_in(g_stp_uart_rx_fifo, (PUINT8)data, count);
#endif

		/*schedule it! */
		tasklet_schedule(&g_stp_uart_rx_fifo_tasklet);
	} else {
		UART_ERR_FUNC("stp_uart_tty_receive rxfifo is full!!\n");
	}

#if 0
	{
		struct timeval now;
		do_gettimeofday(&now);
		pr_warn("[-STP][  ][R] %4d --> sec = %lu, --> usec --> %lu\n",
			count, now.tv_sec, now.tv_usec);
	}
#endif

/* write_unlock(&g_stp_uart_rx_handling_lock); */

}
#elif (LDISC_RX == LDISC_RX_WORK)
static INT32 stp_uart_fifo_init(VOID)
{
	INT32 err = 0;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	g_stp_uart_rx_buf = vzalloc(LDISC_RX_BUF_SIZE);
	if (!g_stp_uart_rx_buf) {
		UART_ERR_FUNC("kfifo_alloc failed (kernel version >= 2.6.37)\n");
		err = -4;
		goto fifo_init_end;
	}
#else
	g_stp_uart_rx_buf = vmalloc(LDISC_RX_BUF_SIZE);
	if (!g_stp_uart_rx_buf) {
		UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.37)\n");
		err = -4;
		goto fifo_init_end;
	}
	memset(g_stp_uart_rx_buf, 0, LDISC_RX_BUF_SIZE);
#endif

	UART_INFO_FUNC("g_stp_uart_rx_buf alloc ok(0x%p, %d)\n",
		       g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);

	/*add rx fifo */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
	spin_lock_init(&g_stp_uart_rx_fifo_spinlock);
	g_stp_uart_rx_fifo =
	    kfifo_alloc(LDISC_RX_FIFO_SIZE, GFP_KERNEL, &g_stp_uart_rx_fifo_spinlock);
	if (NULL == g_stp_uart_rx_fifo) {
		UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.33)\n");
		err = -1;
		goto fifo_init_end;
	}
#else
	/* allocate struct kfifo first */
	g_stp_uart_rx_fifo = kzalloc(sizeof(struct kfifo), GFP_KERNEL);
	if (NULL == g_stp_uart_rx_fifo) {
		err = -2;
		UART_ERR_FUNC("kzalloc struct kfifo failed (kernel version > 2.6.33)\n");
		goto fifo_init_end;
	}

	/* allocate kfifo data buffer then */
	err = kfifo_alloc(g_stp_uart_rx_fifo, LDISC_RX_FIFO_SIZE, GFP_KERNEL);
	if (0 != err) {
		UART_ERR_FUNC("kfifo_alloc failed, err(%d)(kernel version > 2.6.33)\n", err);
		kfree(g_stp_uart_rx_fifo);
		g_stp_uart_rx_fifo = NULL;
		err = -3;
		goto fifo_init_end;
	}
#endif
	UART_INFO_FUNC("g_stp_uart_rx_fifo alloc ok\n");

 fifo_init_end:

	if (0 == err) {
		/* kfifo init ok */
		kfifo_reset(g_stp_uart_rx_fifo);
		UART_DBG_FUNC("g_stp_uart_rx_fifo init success\n");
	} else {
		UART_ERR_FUNC("stp_uart_fifo_init() fail(%d)\n", err);
		if (g_stp_uart_rx_buf) {
			UART_ERR_FUNC("free g_stp_uart_rx_buf\n");
			vfree(g_stp_uart_rx_buf);
			g_stp_uart_rx_buf = NULL;
		}
	}

	return err;
}

static INT32 stp_uart_fifo_deinit(VOID)
{
	if (g_stp_uart_rx_buf) {
		vfree(g_stp_uart_rx_buf);
		g_stp_uart_rx_buf = NULL;
	}

	if (NULL != g_stp_uart_rx_fifo) {
		kfifo_free(g_stp_uart_rx_fifo);
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
		/* do nothing */
#else
		kfree(g_stp_uart_rx_fifo);
#endif
		g_stp_uart_rx_fifo = NULL;
	}
	return 0;
}

static VOID stp_uart_fifo_reset(VOID)
{
	if (NULL != g_stp_uart_rx_fifo) {
		kfifo_reset(g_stp_uart_rx_fifo);
	}
}

static void stp_uart_rx_worker(struct work_struct *work)
{
	UINT32 read;

	if (unlikely(!g_stp_uart_rx_fifo)) {
		UART_ERR_FUNC("NULL rx fifo!\n");
		return;
	}
	if (unlikely(!g_stp_uart_rx_buf)) {
		UART_ERR_FUNC("NULL rx buf!\n");
		return;
	}


	/* run until fifo becomes empty */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
	while (kfifo_len(g_stp_uart_rx_fifo)) {
		read = kfifo_get(g_stp_uart_rx_fifo, g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);
		/* UART_LOUD_FUNC("kfifo_get(%d)\n", read); */
		if (likely(read)) {
			mtk_wcn_stp_parser_data((UINT8 *) g_stp_uart_rx_buf, read);
		}
	}
#else
	while (!kfifo_is_empty(g_stp_uart_rx_fifo)) {
		read = kfifo_out(g_stp_uart_rx_fifo, g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);
		UART_DBG_FUNC("kfifo_out(%d)\n", read);
		/* pr_warn("rx_work:%d\n\r",read); */
		if (likely(read)) {
			/* UART_LOUD_FUNC("->%d\n", read); */
			mtk_wcn_stp_parser_data((UINT8 *) g_stp_uart_rx_buf, read);
			/* UART_LOUD_FUNC("<-\n", read); */
		}
	}
#endif

	return;
}

static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{
	UINT32 written;

	/* UART_LOUD_FUNC("URX:%d\n", count); */
	if (unlikely(count > 2000)) {
		UART_WARN_FUNC("abnormal: buffer count = %d\n", count);
	}

	if (unlikely(!g_stp_uart_rx_fifo || !g_stp_uart_rx_work || !g_stp_uart_rx_wq)) {
		UART_ERR_FUNC
		    ("abnormal g_stp_uart_rx_fifo(0x%p),g_stp_uart_rx_work(0x%p),g_stp_uart_rx_wq(0x%p)\n",
		     g_stp_uart_rx_fifo, g_stp_uart_rx_work, g_stp_uart_rx_wq);
		return;
	}

	/* need to check available buffer size? skip! */

	/* need to lock fifo? skip for single writer single reader! */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33))
	written = kfifo_put(g_stp_uart_rx_fifo, (PUINT8)data, count);
#else
	written = kfifo_in(g_stp_uart_rx_fifo, (PUINT8)data, count);
#endif
	/* pr_warn("uart_rx:%d,wr:%d\n\r",count,written); */

	queue_work(g_stp_uart_rx_wq, g_stp_uart_rx_work);

	if (unlikely(written != count)) {
		UART_ERR_FUNC("c(%d),w(%d) bytes dropped\n", count, written);
	}

	return;
}

#else

static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{

#if 0
	mtk_wcn_stp_debug_gpio_assert(IDX_STP_RX_PROC, DBG_TIE_LOW);
#endif

	if (count > 2000) {
		/*this is abnormal */
		UART_ERR_FUNC("stp_uart_tty_receive buffer count = %d\n", count);
	}
#if 0
	{
		struct timeval now;

		do_gettimeofday(&now);

		pr_warn("[+STP][  ][R] %4d --> sec = %d, --> usec --> %d\n",
			count, now.tv_sec, now.tv_usec);
	}
#endif


	/*There are multi-context to access here? Need to spinlock? */
	/*Only one context: flush_to_ldisc in tty_buffer.c */
	mtk_wcn_stp_parser_data((UINT8 *) data, (UINT32) count);

#if 0
	mtk_wcn_stp_debug_gpio_assert(IDX_STP_RX_PROC, DBG_TIE_HIGH);
#endif

	tty_unthrottle(tty);

#if 0
	{
		struct timeval now;

		do_gettimeofday(&now);

		pr_warn("[-STP][  ][R] %4d --> sec = %d, --> usec --> %d\n",
			count, now.tv_sec, now.tv_usec);
	}
#endif
	return;
}
#endif

static int stp_uart_tty_ioctl(struct tty_struct *tty, struct file *file,
			      unsigned int cmd, unsigned long arg)
{
	INT32 err = 0;

    UART_DBG_FUNC("%s =>\n", __FUNCTION__);

	switch (cmd) {
	case HCIUARTSETPROTO:
		UART_DBG_FUNC("<!!> Set low_latency to TRUE <!!>\n");
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 10, 0))
		tty->port->low_latency = 1;
#else
		tty->low_latency = 1;
#endif

		break;
	default:
		UART_DBG_FUNC("<!!> n_tty_ioctl_helper <!!>\n");
		err = n_tty_ioctl_helper(tty, file, cmd, arg);
		break;
	};
    UART_DBG_FUNC("%s <=\n", __FUNCTION__);

	return err;
}

static ssize_t stp_uart_tty_read(struct tty_struct *tty, struct file *file,
				 unsigned char __user *buf, size_t nr)
{
	return 0;
}

static ssize_t stp_uart_tty_write(struct tty_struct *tty, struct file *file,
				  const unsigned char *data, size_t count)
{
	return 0;
}

static unsigned int stp_uart_tty_poll(struct tty_struct *tty, struct file *filp, poll_table *wait)
{
	return 0;
}

INT32 mtk_wcn_uart_tx(const PUINT8 data, const UINT32 size, PUINT32 written_size)
{
	INT32 room;
	/* int idx = 0; */
	/* unsigned long flags; */
	UINT32 len;
	/* static int tmp=0; */
	static INT32 i;
	if (stp_tty == NULL)
		return -1;
	UART_DBG_FUNC("++\n");
	(*written_size) = 0;

	/* put data into ring buffer */
	/* down(&buf_mtx); */


	/*
	   [PatchNeed]
	   spin_lock_irqsave is redundant
	 */
	/* spin_lock_irqsave(&buf_lock, flags); */

	room =
	    (wr_idx >=
	     rd_idx) ? (MTKSTP_BUFFER_SIZE - (wr_idx - rd_idx) - 1) : (rd_idx - wr_idx - 1);
	UART_DBG_FUNC("r(%d)s(%d)wr_i(%d)rd_i(%d)\n\r", room, size, wr_idx, rd_idx);
	/*
	   [PatchNeed]
	   Block copy instead of byte copying
	 */
	if (data == NULL) {
		UART_ERR_FUNC("pid(%d)(%s): data is NULL\n", current->pid, current->comm);
		(*written_size) = 0;
		UART_DBG_FUNC("--\n");
		return -2;
	}
#if 1
	if (unlikely(size > room)) {
		UART_ERR_FUNC
		    ("pid(%d)(%s)room is not available, size needed(%d), wr_idx(%d), rd_idx(%d), room left(%d)\n",
		     current->pid, current->comm, size, wr_idx, rd_idx, room);
		UART_DBG_FUNC("--\n");
		(*written_size) = 0;
		return -3;
	} else {
		/*
		   wr_idx : the position next to write
		   rd_idx : the position next to read
		 */
		len = min(size, MTKSTP_BUFFER_SIZE - (UINT32)wr_idx);
		memcpy(&tx_buf[wr_idx], &data[0], len);
		memcpy(&tx_buf[0], &data[len], size - len);
		wr_idx = (wr_idx + size) % MTKSTP_BUFFER_SIZE;
		UART_DBG_FUNC("r(%d)s(%d)wr_i(%d)rd_i(%d)\n\r", room, size, wr_idx, rd_idx);
		i++;
		if (size < 0) {
			UART_ERR_FUNC
			    ("Error(i-%d):[pid(%d)(%s)]len(%d)size(%d)wr_i(%d)rd_i(%d)\n\r", i,
			     current->pid, current->comm, len, size, wr_idx, rd_idx);
			(*written_size) = 0;
		} else if (size == 0) {
			(*written_size) = 0;
		} else if (size < MAX_PACKET_ALLOWED) {
			/* only size ~(0, 2000) is allowed */
			(*written_size) = stp_uart_tx_wakeup(stp_tty);
			if (*written_size < 0) {
				/* reset read and write index of tx_buffer, is there any risk? */
				wr_idx = rd_idx = 0;
				*written_size = 0;
			}
		} else {
			/* we filter all packet with size > 2000 */
			UART_ERR_FUNC
			    ("Warnning(i-%d):[pid(%d)(%s)]len(%d)size(%d)wr_i(%d)rd_i(%d)\n\r", i,
			     current->pid, current->comm, len, size, wr_idx, rd_idx);
			(*written_size) = 0;
		}
	}
#endif


#if 0
	while ((room > 0) && (size > 0)) {
		tx_buf[wr_idx] = data[idx];
		wr_idx = ((wr_idx + 1) % MTKSTP_BUFFER_SIZE);
		idx++;
		room--;
		size--;
		(*written_size)++;
	}
#endif
	/* up(&buf_mtx); */
	/*
	   [PatchNeed]
	   spin_lock_irqsave is redundant
	 */
/* //    spin_lock_irqsave(&buf_lock, flags); */

	/*[PatchNeed]To add a tasklet to shedule Uart Tx */
	UART_DBG_FUNC("--\n");
	return 0;
}

static int mtk_wcn_stp_uart_init(void)
{
	static struct tty_ldisc_ops stp_uart_ldisc;
	INT32 err;
	INT32 fifo_init_done = 0;

	UART_INFO_FUNC("mtk_wcn_stp_uart_init(): MTK STP UART driver\n");

#if  (LDISC_RX == LDISC_RX_TASKLET)
	err = stp_uart_fifo_init();
	if (err != 0) {
		goto init_err;
	}
	fifo_init_done = 1;
	/*init rx tasklet */
	tasklet_init(&g_stp_uart_rx_fifo_tasklet, stp_uart_rx_handling, (unsigned long)0);

#elif (LDISC_RX == LDISC_RX_WORK)
	err = stp_uart_fifo_init();
	if (err != 0) {
		UART_ERR_FUNC("stp_uart_fifo_init(WORK) error(%d)\n", err);
		err = -EFAULT;
		goto init_err;
	}
	fifo_init_done = 1;

	g_stp_uart_rx_work = vmalloc(sizeof(struct work_struct));
	if (!g_stp_uart_rx_work) {
		UART_ERR_FUNC("vmalloc work_struct(%d) fail\n", sizeof(struct work_struct));
		err = -ENOMEM;
		goto init_err;
	}

	g_stp_uart_rx_wq = create_singlethread_workqueue("mtk_urxd");
	if (!g_stp_uart_rx_wq) {
		UART_ERR_FUNC("create_singlethread_workqueue fail\n");
		err = -ENOMEM;
		goto init_err;
	}

	/* init rx work */
	INIT_WORK(g_stp_uart_rx_work, stp_uart_rx_worker);

#endif

	/* Register the tty discipline */
	memset(&stp_uart_ldisc, 0, sizeof(stp_uart_ldisc));
	stp_uart_ldisc.magic = TTY_LDISC_MAGIC;
	stp_uart_ldisc.name = "n_mtkstp";
	stp_uart_ldisc.open = stp_uart_tty_open;
	stp_uart_ldisc.close = stp_uart_tty_close;
	stp_uart_ldisc.read = stp_uart_tty_read;
	stp_uart_ldisc.write = stp_uart_tty_write;
	stp_uart_ldisc.ioctl = stp_uart_tty_ioctl;
	stp_uart_ldisc.poll = stp_uart_tty_poll;
	stp_uart_ldisc.receive_buf = stp_uart_tty_receive;
	stp_uart_ldisc.write_wakeup = stp_uart_tty_wakeup;
	stp_uart_ldisc.owner = THIS_MODULE;

	if ((err = tty_register_ldisc(N_MTKSTP, &stp_uart_ldisc))) {
		UART_ERR_FUNC("MTK STP line discipline registration failed. (%d)\n", err);
		goto init_err;
	}

	/*
	   mtk_wcn_stp_register_if_tx( mtk_wcn_uart_tx);
	 */

	return 0;

 init_err:

#if (LDISC_RX == LDISC_RX_TASKLET)
	/* nothing */
	if (fifo_init_done) {
		stp_uart_fifo_deinit();
	}
#elif (LDISC_RX == LDISC_RX_WORK)
	if (g_stp_uart_rx_wq) {
		destroy_workqueue(g_stp_uart_rx_wq);
		g_stp_uart_rx_wq = NULL;
	}
	if (g_stp_uart_rx_work) {
		vfree(g_stp_uart_rx_work);
	}
	if (fifo_init_done) {
		stp_uart_fifo_deinit();
	}
#endif
	UART_ERR_FUNC("init fail, return(%d)\n", err);

	return err;

}

static void mtk_wcn_stp_uart_exit(void)
{
	INT32 err;

	mtk_wcn_stp_register_if_tx(STP_UART_IF_TX, NULL);	/* unregister if_tx function */

	/* Release tty registration of line discipline */
	if ((err = tty_unregister_ldisc(N_MTKSTP))) {
		UART_ERR_FUNC("Can't unregister MTK STP line discipline (%d)\n", err);
	}
#if (LDISC_RX == LDISC_RX_TASKLET)
	tasklet_kill(&g_stp_uart_rx_fifo_tasklet);
	stp_uart_fifo_deinit();
#elif (LDISC_RX == LDISC_RX_WORK)
	if (g_stp_uart_rx_work) {
		cancel_work_sync(g_stp_uart_rx_work);
	}
	if (g_stp_uart_rx_wq) {
		destroy_workqueue(g_stp_uart_rx_wq);
		g_stp_uart_rx_wq = NULL;
	}
	if (g_stp_uart_rx_work) {
		vfree(g_stp_uart_rx_work);
		g_stp_uart_rx_work = NULL;
	}
	stp_uart_fifo_deinit();

#endif
	return;
}

#ifdef MTK_WCN_REMOVE_KERNEL_MODULE

INT32 mtk_wcn_stp_uart_drv_init(VOID)
{
	return mtk_wcn_stp_uart_init();

}

VOID mtk_wcn_stp_uart_drv_exit(VOID)
{
	return mtk_wcn_stp_uart_exit();
}


EXPORT_SYMBOL(mtk_wcn_stp_uart_drv_init);
EXPORT_SYMBOL(mtk_wcn_stp_uart_drv_exit);
#else

module_init(mtk_wcn_stp_uart_init);
module_exit(mtk_wcn_stp_uart_exit);

#endif



MODULE_LICENSE("GPL");
MODULE_AUTHOR("MediaTek Inc WCN_SE_CS3");
MODULE_DESCRIPTION("STP-HIF UART Interface");
