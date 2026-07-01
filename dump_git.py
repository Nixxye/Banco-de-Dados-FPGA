import subprocess
with open("git_log_test_hw.txt", "w") as f:
    subprocess.run(["git", "log", "-p", "ProjetoFinal/test_hw.py"], stdout=f, cwd="c:/Users/cjean/Documents/Git/Logica-Final")
