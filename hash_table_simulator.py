class SimulatedMutex:
    def __init__(self):
        self.is_locked = False
        self.last_unlock_time = -1

    def try_acquire(self, current_time):
        if not self.is_locked and current_time > self.last_unlock_time:
            self.is_locked = True
            return 'acquired'
        return 'waiting'

    def release(self, current_time):
        if self.is_locked:
            self.is_locked = False
            self.last_unlock_time = current_time
            return 'released'
        return 'error'

class SimulatedSharedMutex:
    def __init__(self):
        self.reader_count = 0
        self.writer = False
        self.last_unlock_time = -1

    def try_acquire_read(self, current_time):
        if not self.writer and current_time > self.last_unlock_time:
            self.reader_count += 1
            return 'acquired_read'
        return 'waiting_read'

    def try_acquire_write(self, current_time):
        if self.reader_count == 0 and not self.writer and current_time > self.last_unlock_time:
            self.writer = True
            return 'acquired_write'
        return 'waiting_write'

    def release_read(self, current_weak_time):
        if self.reader_count > 0:
            self.reader_count -= 1
            if self.reader_count == 0:
                self.last_unlock_time = current_weak_time
            return 'released_read'
        return 'error_read'

    def release_write(self, current_weak_time):
        if self.writer:
            self.writer = False
            self.last_unlock_time = current_weak_time
            return 'released_write'
        return 'error_write'

def hash_key(key):
    return key % 10

class HashTable:
    def __init__(self, num_slots, lock_type):
        self.data = [None] * num_slots
        self.lock_type = lock_type
        if lock_type == 'global':
            self.locks = SimulatedMutex()
        elif lock_type == 'per_slot':
            self.locks = [SimulatedMutex() for _ in range(num_slots)]
        elif lock_type == 'shared_per_slot':
            self.locks = [SimulatedSharedMutex() for _ in range(num_slots)]


class SimulatedThread:
    def __init__(self, thread_id, hash_table, operations):
        self.thread_id = f"Thread-{thread_id}"
        self.hash_table = hash_table
        self.operations = operations
        self.current_operation = 0
        self.current_index = 0
        self.initial_indexes = [-1] * len(operations)  # Track initial index for each operation
        self.action_state = 'lock'
        self.done = False

    def step(self, current_time):

        op_type, key, value = self.operations[self.current_operation]
        index = (hash_key(key) + self.current_index) % len(self.hash_table.data)
        lock = self.hash_table.locks if self.hash_table.lock_type == 'global' else self.hash_table.locks[index]

        # Set initial index first time operation runs
        if self.initial_indexes[self.current_operation] == -1:  
            self.initial_indexes[self.current_operation] = index

        if self.action_state == 'lock':
            if self.hash_table.lock_type == 'shared_per_slot':
                result = lock.try_acquire_write(current_time) if op_type == 'insert' else lock.try_acquire_read(current_time)
                print(f"{self.thread_id}: Attempt to lock for {op_type} on slot {index} - {result}")
            elif self.hash_table.lock_type == 'per_slot':
                result = lock.try_acquire(current_time)
                print(f"{self.thread_id}: Attempt to lock slot {index} - {result}")
            else:                
                result = lock.try_acquire(current_time)
                print(f"{self.thread_id}: Attempt to lock table - {result}")

            if result.startswith('acquired'):
                self.action_state = 'action'
            elif 'waiting' in result:
                self.current_index = (self.current_index) % len(self.hash_table.data)

        elif self.action_state == 'action':
            if op_type == 'insert':
                if self.hash_table.data[index] is None:
                    self.hash_table.data[index] = key
                    print(f"{self.thread_id}: Inserted key {key} at slot {index}")
                    self.action_state = 'unlock'
                else:
                    another_key = self.hash_table.data[index]
                    print(f"{self.thread_id}: Found another key {another_key} at slot {index}")                    
                    self.action_state = 'unlock_and_relock'

            elif op_type == 'find':
                if self.hash_table.data[index] == key:
                    print(f"{self.thread_id}: Key {key} found at slot {index}")
                    self.action_state = 'unlock'
                elif self.hash_table.data[index] == None:
                    print(f"{self.thread_id}: Key {key} not found.")
                    self.action_state = 'unlock'
                else:
                    another_key = self.hash_table.data[index]
                    print(f"{self.thread_id}: Found another key {another_key} at slot {index}")
                    self.action_state = 'unlock_and_relock'

        elif self.action_state == 'unlock':
            if self.hash_table.lock_type == 'shared_per_slot':
                result = lock.release_write(current_time) if op_type == 'insert' else lock.release_read(current_time)
                print(f"{self.thread_id}: Released lock for {op_type} on slot {index} - {result}")
            elif self.hash_table.lock_type == 'per_slot':
                result = lock.release(current_time)
                print(f"{self.thread_id}: Released lock on slot {index} - {result}")
            else:
                result = lock.release(current_time)
                print(f"{self.thread_id}: Released lock on table - {result}")

            # Move to next operation if we looped back to the start
            self.current_operation += 1  
            self.current_index = 0
            self.action_state = 'lock'

        elif self.action_state == 'unlock_and_relock':
            print("unlock_and_relock")
            if self.hash_table.lock_type == 'shared_per_slot':
                result = lock.release_write(current_time) if op_type == 'insert' else lock.release_read(current_time)
                print(f"{self.thread_id}: Released lock for {op_type} on slot {index} - {result}")
            elif self.hash_table.lock_type == 'per_slot':
                result = lock.release(current_time)
                print(f"{self.thread_id}: Released lock on slot {index} - {result}")
            else:
                result = lock.release(current_time)
                print(f"{self.thread_id}: Released lock on table - {result}")

            self.current_index = (self.current_index + 1) % len(self.hash_table.data)
            self.action_state = 'lock'

        if self.done or self.current_operation >= len(self.operations):
            self.done = True
            return

def simulate(lock_type):
    hash_table = HashTable(10, lock_type)
    threads = [
        SimulatedThread(0, hash_table, [
            ('insert', 25, 'Value25'),  
            ('find', 15, None)          
        ]),
        SimulatedThread(1, hash_table, [
            ('insert', 35, 'Value35'),  
            ('find', 25, None),         
        ]),
        SimulatedThread(2, hash_table, [
            ('find', 45, None), 
            ('find', 25, None),         
        ])
    ]

    time_step = 1
    all_done = False
    while not all_done:
        all_done = True
        print(f"\nTime Step: {time_step}")
        for thread in threads:
            if not thread.done:
                thread.step(time_step)
                all_done = False
        time_step += 1
        if time_step >= 100:  # Safeguard against infinite loops
            print("Stopping simulation after 100 steps to prevent a possible infinite loop.")
            break

for lock_type in ['global', 'per_slot', 'shared_per_slot']:
    print("\n\n---------------------------------")
    print(f"Simulation with {lock_type} locking:")
    print("---------------------------------")
    simulate(lock_type)
