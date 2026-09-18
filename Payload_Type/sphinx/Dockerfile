FROM itsafeaturemythic/mythic_python_base:latest

RUN apt-get update && apt-get install -y \
    build-essential \
    mingw-w64 \
    nasm \
    make \
    && rm -rf /var/lib/apt/lists/*

RUN python3 -m pip install donut-shellcode

WORKDIR /Mythic/

CMD ["python3", "main.py"]
