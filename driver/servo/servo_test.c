#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define SERVO_SET_ANGLE _IOW('S', 1, int)
#define SERVO_GET_ANGLE _IOR('S', 2, int)

int main()
{
    int fd;
    int angle;
    char input[10];
    
    fd = open("/sys/class/pwm/pwmchip2/pwm0/duty_cycle", O_RDWR);
    if (fd < 0) {
        perror("Failed to open device");
        return 1;
    }
    
    printf("Servo Control Program\n");
    printf("Enter angle (0-180) or 'q' to quit:\n");
    
    while (1) {
        printf("> ");
        if (fgets(input, sizeof(input), stdin) == NULL)
            break;
            
        if (input[0] == 'q' || input[0] == 'Q')
            break;
            
        angle = atoi(input);
        if (angle < 0 || angle > 180) {
            printf("Please enter angle between 0 and 180\n");
            continue;
        }
        
        if (ioctl(fd, SERVO_SET_ANGLE, &angle) < 0) {
            perror("ioctl failed");
            break;
        }
        
        printf("Servo moved to %d degrees\n", angle);
    }
    
    close(fd);
    printf("Goodbye!\n");
    return 0;
}