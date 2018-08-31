// AsyncActionCallback.h

#ifndef _ASYNCACTIONCALLBACK_h
#define _ASYNCACTIONCALLBACK_h


#define _TASK_OO_CALLBACKS
#include <TaskSchedulerDeclarations.h>
#include <RingBufCPP.h>
#include <Callback.h>


#define ASYNC_RECEIVER_QUEUE_MAX_QUEUE_DEPTH 5

class AsyncActionCallback : Task
{
private:
	Signal<uint8_t> ActionCallback;
	uint8_t Grunt;
	RingBufCPP<uint8_t, ASYNC_RECEIVER_QUEUE_MAX_QUEUE_DEPTH> EventQueue;

public:
	AsyncActionCallback(Scheduler* scheduler)
		: Task(0, TASK_FOREVER, scheduler, false)
	{
	}

	void AttachActionCallback(const Slot<uint8_t>& slot)
	{
		ActionCallback.attach(slot);
	}

	void AppendEventToQueue(const uint8_t actionCode)
	{
		Grunt = actionCode;
		EventQueue.addForce(Grunt);
		enable();
	}

	bool OnEnable()
	{
		return true;
	}

	void OnDisable()
	{
	}

	bool Callback()
	{
		if (EventQueue.isEmpty())
		{
			disable();
			return false;
		}

		EventQueue.pull(Grunt);
		ActionCallback.fire(Grunt);

		return true;
	}
};
#endif