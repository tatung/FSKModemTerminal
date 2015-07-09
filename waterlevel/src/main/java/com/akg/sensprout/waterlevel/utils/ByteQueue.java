package com.akg.sensprout.waterlevel.utils;

/**
 * Created by tatung on 7/9/15.
 */
public class ByteQueue {
    private byte[] queue;
    private int front = -1;
    private int back = -1;

    public ByteQueue() {
        queue = new byte[4];
    }

    public ByteQueue(int size) {
        queue = new byte[size];
    }

    public void enqueue(byte in) throws Exception {
        if (isEmpty()) {
            front++;
            back++;
            queue[back] = in;
        } else {
            if (back == front - 1) {
                throw new Exception("Queue overflow");
            } else {
                back = (back + 1) % queue.length;
                queue[back] = in;
            }
        }
    }

    public byte dequeue() throws Exception {
        byte res;
        if (isEmpty()) {
            throw new Exception("Queue empty");
        } else if (front == back) {
            res = queue[front];
            setEmpty();
        } else {
            res = queue[front];
            front = (front + 1) % queue.length;
        }

        return res;
    }

    public boolean isEmpty() {
        return front == -1 && back == -1;
    }

    public void setEmpty() {
        front = -1;
        back = -1;
    }

    public int length(){
        if(front == -1 && back == -1){
            return 0;
        } else if(front <= back){
            return back - front + 1;
        } else {
            return queue.length - front + back + 1;
        }
    }
}
