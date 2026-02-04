# Task Locks

Each claimed task must create a lock directory:

`docs/claims/task-<id>.lock/`

The claim scripts create these atomically to enforce a mutex.
