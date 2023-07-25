import subprocess
import concurrent.futures
import glob

# List of C++ files to compile
# Use glob to get all .cpp files
cpp_files = glob.glob('*.cpp')

def compile_cpp(file):
    # Define the command to compile the C++ file
    command = f"g++ -fdiagnostics-color -std=c++11 -std=c++14 -O3 -Wall -Werror -Wextra {file} -o {file.split('.')[0]}.out"
    
    # Run the command
    process = subprocess.Popen(command, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    
    # Wait for the process to complete and get the output
    stdout, stderr = process.communicate()
    
    # Check if the process had an error
    if process.returncode != 0:
        print(f"Error while compiling {file}:")
        print(stderr.decode())
    else:
        print(f"Compiled {file} successfully.")
        print(stdout.decode())

if __name__ ==  '__main__':
    # Create a ProcessPoolExecutor
    with concurrent.futures.ProcessPoolExecutor() as executor:
        # Use the executor to compile all C++ files in parallel
        executor.map(compile_cpp, cpp_files)