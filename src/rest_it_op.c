#include <errno.h>
#include <pthread.h>

#include <osmocom/core/it_q.h>

#include "rest_it_op.h"
#include "internal.h"

/***********************************************************************
 * HTTP THREAD
 ***********************************************************************/

struct rest_it_op *rest_it_op_alloc(void *ctx)
{
	struct rest_it_op *op = talloc_zero(ctx, struct rest_it_op);
	if (!op)
		return NULL;
	pthread_mutex_init(&op->mutex, NULL);
	pthread_cond_init(&op->cond, NULL);

	return op;
}

/* enqueue an inter-thread operation in REST->main direction and wait for its completion */
int rest_it_op_send_and_wait(struct rest_it_op *op, unsigned int wait_sec)
{
	struct timespec ts;
	int rc = 0;

	rc = osmo_it_q_enqueue(g_cbc->it_q.rest2main, op, list);
	if (rc < 0)
		return rc;

	/* grab mutex before pthread_cond_timedwait() */
	pthread_mutex_lock(&op->mutex);
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += wait_sec;

	while (rc == 0)
		rc = pthread_cond_timedwait(&op->cond, &op->mutex, &ts);

	if (rc == 0)
		pthread_mutex_unlock(&op->mutex);

	/* 'op' is implicitly owned by the caller again now, who needs to take care
	 * of releasing its memory */

	return rc;
}



/***********************************************************************
 * MAIN THREAD
 ***********************************************************************/


void rest2main_read_cb(struct osmo_it_q *q, void *item)
{
	struct rest_it_op *op = item;
	struct cbc_message *cbc_msg;
	/* FIXME: look up related message and dispatch to message FSM,
	 * which will eventually call pthread_cond_signal(&op->cond) */
	switch (op->operation) {
	case REST_IT_OP_MSG_CREATE:
		/* FIXME: send to message FSM who can addd it on RAN */
		cbc_message_new(&op->u.create.cbc_msg);
		break;
	case REST_IT_OP_MSG_DELETE:
		/* FIXME: send to message FSM who can remove it from RAN */
		cbc_msg = cbc_message_by_id(op->u.del.msg_id);
		if (cbc_msg) {
			llist_del(&cbc_msg->list);
			talloc_free(cbc_msg);
		}
		break;
	default:
		break;
	}
	pthread_cond_signal(&op->cond); // HACK
}
