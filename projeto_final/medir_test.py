import sys
import time
import resource
import subprocess

cfg = sys.argv[1] if len(sys.argv) > 1 else "config_multi.yaml"
t0 = time.perf_counter()
proc = subprocess.Popen(["./runner_yaml", cfg])
proc.wait()
dt = time.perf_counter() - t0
ru = resource.getrusage(resource.RUSAGE_CHILDREN)

print("\n=== MÉTRICAS DO PROCESSO (getrusage) ===")
print(f"Tempo total externo : {dt:.4f} s")
print(f"Max RSS             : {ru.ru_maxrss} KB ({ru.ru_maxrss / 1024:.2f} MB)")
print(f"Minor page faults   : {ru.ru_minflt}")
print(f"Major page faults   : {ru.ru_majflt}")
print(f"Voluntary ctx sw    : {ru.ru_nvcsw}")
print(f"Involuntary ctx sw  : {ru.ru_nivcsw}")