// No headers or standardlib, we have to define constants ourselfes
#define O_RDONLY 0x0
#define O_NONBLOCK 0x800

// No libc either - defining all systemcalls by hand
#define exit(code) asm("int $0x80;" ::"a"(1), "b"(code))

#define write(fd, str, len) asm volatile("int $0x80" ::"a"(4), "b"(fd), "c"(str), "d"(len) \
                                         : "memory")

// Very dirty hack
int read_result;

#define read(fd, buf, len) asm volatile("int $0x80"                           \
                                        : "=&a"(read_result)                  \
                                        : "a"(3), "b"(fd), "c"(buf), "d"(len) \
                                        : "memory")

#define ioctl(fno, code, data) asm volatile("int $0x80" ::"a"(54), "b"(fno), "c"(code), "d"(data) \
                                            : "memory")

#define fcntl(fno, code, data) asm volatile("int $0x80" ::"a"(55), "b"(fno), "c"(code), "d"(data) \
                                            : "memory")

#define open(fno, path, flags) asm volatile("int $0x80"                     \
                                            : "=&a"(fno)                    \
                                            : "a"(5), "b"(path), "c"(flags) \
                                            : "memory")

#define close(fd) asm volatile("int $0x80" ::"a"(6), "b"(fd) \
                               : "memory")

#define nanosleep(timespec) asm volatile("int $0x80" ::"a"(162), "b"(timespec), "c"(0) \
                                         : "memory")

// Useful macros
#define clear() write(1, "\e[1;1H\e[2J", 11)
#define static_print(text) write(1, text, sizeof(text))
#define print(text, len) write(1, text, len)

// Math stuff
#define abs(expr) (expr >= 0) ? (expr) : -(expr)

// Sleep function
unsigned timespec[2];

void sleep_ms(unsigned long nsecs)
{
    timespec[0] = 0;
    timespec[1] = nsecs * 1000000L;
    nanosleep(timespec);
}

// Set console mode to raw (in order to read stdin nonblocking)
char ioctlbuf[36];

void raw_console()
{
    // Non-blocking stdin
    fcntl(0, 4, O_RDONLY | O_NONBLOCK);

    // Raw Terminal
    ioctl(0, 0x5401, ioctlbuf);
    ioctlbuf[12] &= !0x00000002;
    ioctl(0, 0x5402, ioctlbuf);
}

// Simple function for generating random bytes
int fd;

void rand(char *buf, int amount)
{
    open(fd, "/dev/random", O_RDONLY);
    read(fd, buf, amount);
    close(fd);
}

/*
    2 Bytes per Block:

    0 1 0 0
    0 1 1 0 -> Byte 1 = 0 1 0 0 0 1 1 0
    0 0 1 0
    0 0 0 0 -> Byte 2 = 0 0 1 0 0 0 0 0
*/

int blocks[] = {
    // #
    // ##
    //  #
    0b0100011000100000,
    // ##
    // ##
    0b0000011001100000,
    // #
    // #
    // #
    // #
    0b0100010001000100,
    //  #
    // ###
    0b0100111000000000};

#define N_BLOCKS 4

int read_block(int type, int x, int y, int rotation)
{
    switch (rotation)
    {
    case 0:
        return (blocks[type] >> (y * 4 + x)) & 1;
    case 1:
        return (blocks[type] >> (x * 4 + y)) & 1;
    case 2:
        return (blocks[type] >> ((4 - y) * 4 + x)) & 1;
    case 3:
        return (blocks[type] >> ((4 - x) * 4 + y)) & 1;
    }

    return 0;
}

#define GAMESIZE 15
#define BLK_WALL 1
#define BLK_BLOCK 2
#define BLK_FALLING 3

// [y][x]
char field[GAMESIZE][GAMESIZE];

void copy_row(char *src, char *dest)
{
    for (int i = 0; i < GAMESIZE; i++)
        dest[i] = src[i];
}

// Currently falling block
struct
{
    int x;
    int y;
    int type;
    int rotation;

    int move_request;
    int rotation_request;
} current_block;

char randbuf[2];

void spawn_block()
{
    rand(randbuf, 2);

    current_block.type = abs(randbuf[0] % N_BLOCKS);
    current_block.x = abs(randbuf[1] % (GAMESIZE - 6)) + 1;
    current_block.y = 1;
}

char linebuf[GAMESIZE + 1];

void draw_game()
{
    for (int y = 0; y < GAMESIZE; y++)
    {
        for (int x = 0; x < GAMESIZE; x++)
        {
            switch (field[y][x])
            {
            case BLK_BLOCK:
            case BLK_WALL:
                linebuf[x] = '#';
                break;
            case BLK_FALLING:
                linebuf[x] = '+';
                break;
            default:
                linebuf[x] = ' ';
            }
        }
        linebuf[GAMESIZE] = '\n';
        print(linebuf, GAMESIZE + 1);
    }
}

void rotate_block_if_possible()
{

    int next_rotation = (current_block.rotation + 1) % 4;

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            int obstacle = field[y + current_block.y][current_block.x + x];

            if (read_block(current_block.type, x, y, next_rotation) && (obstacle == BLK_BLOCK || obstacle == BLK_WALL))
                // Goto saves a few instructions here
                goto impossible;
        }
    }

    current_block.rotation = next_rotation;

impossible:
    return;
}

void move_sideways_if_possible(int increment)
{
    int nextX = current_block.x + increment;

    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            int obstacle = field[y + current_block.y][nextX + x];

            if (read_block(current_block.type, x, y, current_block.rotation) && (obstacle == BLK_BLOCK || obstacle == BLK_WALL))
                // Goto saves a few instructions here
                goto impossible;
        }
    }

    current_block.x = nextX;

impossible:
    return;
}

int move_block(int move_down)
{
    int can_move = 1;

    // Remove block from current position
    if (current_block.y != 0)
    {
        for (int y = 0; y < 4; y++)
        {
            for (int x = 0; x < 4; x++)
            {
                if (read_block(current_block.type, x, y, current_block.rotation))
                {
                    field[current_block.y + y][current_block.x + x] = 0;

                    // Can this block fall further down?

                    char below = field[current_block.y + y + 1][current_block.x + x];
                    if (below == BLK_BLOCK || below == BLK_WALL)
                    {
                        can_move = 0;
                    }
                }
            }
        }
    }

    move_sideways_if_possible(current_block.move_request);
    if (current_block.rotation_request)
        rotate_block_if_possible();

    if (can_move && move_down)
        current_block.y++;

    // Add block to next position
    for (int y = 0; y < 4; y++)
    {
        for (int x = 0; x < 4; x++)
        {
            if (read_block(current_block.type, x, y, current_block.rotation))
            {
                if (can_move)
                    field[current_block.y + y][current_block.x + x] = BLK_FALLING;
                else
                    field[current_block.y + y][current_block.x + x] = BLK_BLOCK;
            }
        }
    }

    return can_move;
}

void delete_rows_if_possible()
{

    for (int y = 1; y < GAMESIZE - 1; y++)
    {
        int can_delete = 1;

        for (int x = 1; x < GAMESIZE - 1; x++)
        {
            if (field[y][x] != BLK_BLOCK)
            {
                can_delete = 0;
                break;
            }
        }

        if (can_delete)
        {
            for (int yy = y; yy > 2; yy--)
            {
                // Copy row[y-1] to row[y]
                // We can assume that row[1] is 00000.... or the game would already be lost
                copy_row(field[yy - 1], field[yy]);
            }
        }
    }
}

char keybuf;

// This is our 'main' function
void _start()
{
    raw_console();

    // Default field (walls)
    for (int i = 0; i < GAMESIZE; i++)
    {
        field[0][i] = BLK_WALL;
        field[GAMESIZE - 1][i] = BLK_WALL;
        field[i][0] = BLK_WALL;
        field[i][GAMESIZE - 1] = BLK_WALL;
    }

    spawn_block();

    int gamectr = 0;

    // Main loop
    for (;;)
    {
        current_block.move_request = 0;
        current_block.rotation_request = 0;

        keybuf = 0;
        read(0, &keybuf, 1);

        if (keybuf == 'q')
        {
            exit(0);
        }

        if (keybuf == 'a')
        {
            current_block.move_request = -1;
        }

        if (keybuf == 'd')
        {
            current_block.move_request = 1;
        }

        if (keybuf == 'r')
        {
            current_block.rotation_request = 1;
        }

        // Read remaining keystrokes in buffer
        // Bahhhhhh!
        while (read_result > 0)
            read(0, &keybuf, 1);

        if (!move_block(gamectr % 5 == 0))
        {
            // Cant move and y == 1 -> lost game
            if (current_block.y <= 1)
            {
                static_print("You lost");

                // End here
                exit(0);
            }
            // Spawn new block
            spawn_block();
        }

        delete_rows_if_possible();

        clear();
        static_print("a = left, d = right, w = rotate\n");
        draw_game();

        sleep_ms(100);
        gamectr++;
    }

    exit(0);
}
