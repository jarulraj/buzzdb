import time

start = time.time()

sum = 0
for i in range(100000000):
    sum += i

print("Sum: " + str(sum))

end = time.time()
print(f"Time taken: {end - start} seconds")
