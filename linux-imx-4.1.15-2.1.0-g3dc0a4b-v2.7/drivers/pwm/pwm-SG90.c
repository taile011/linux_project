#include <linux/module.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/uaccess.h>
#include <linux/pwm.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/string.h>

#define DEVICE_NAME "sg90"
#define CLASS_NAME "sg90"

#define SERVO_MIN_ANGLE 0
#define SERVO_MAX_ANGLE 180
#define SERVO_MIN_DUTY_NS 500000    // 0.5ms
#define SERVO_MAX_DUTY_NS 2500000   // 2.5ms
#define SERVO_PERIOD_NS 20000000    // 20ms (50Hz)

#define SERVO_SET_ANGLE _IOW('S', 1, int)
#define SERVO_GET_ANGLE _IOR('S', 2, int)

struct servo_device {
    struct pwm_device *pwm;
    struct cdev cdev;
    dev_t devno;
    struct class *class;
    struct device *device;
    int current_angle;
};

static struct servo_device *servo_dev;

static int servo_set_angle(int angle)
{
    int duty_ns;
    int ret;
    
    // 限制角度范围
    if (angle < SERVO_MIN_ANGLE)
        angle = SERVO_MIN_ANGLE;
    if (angle > SERVO_MAX_ANGLE)
        angle = SERVO_MAX_ANGLE;
    
    // 计算占空比
    duty_ns = SERVO_MIN_DUTY_NS + 
              (angle * (SERVO_MAX_DUTY_NS - SERVO_MIN_DUTY_NS)) / 
              (SERVO_MAX_ANGLE - SERVO_MIN_ANGLE);
    
    pr_info("Setting servo: angle=%d°, duty_cycle=%dns\n", angle, duty_ns);
    
    // 配置PWM
    ret = pwm_config(servo_dev->pwm, duty_ns, SERVO_PERIOD_NS);
    if (ret < 0) {
        pr_err("Failed to configure PWM: %d\n", ret);
        return ret;
    }
    
    // 启用PWM输出
    ret = pwm_enable(servo_dev->pwm);
    if (ret < 0) {
        pr_err("Failed to enable PWM: %d\n", ret);
        return ret;
    }
    
    servo_dev->current_angle = angle;
    pr_info("Servo successfully set to %d degrees\n", angle);
    
    return 0;
}

static long servo_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    int angle;

    switch (cmd) {
    case SERVO_SET_ANGLE:
        if (copy_from_user(&angle, (int __user *)arg, sizeof(angle)))
            return -EFAULT;
        
        ret = servo_set_angle(angle);
        break;
        
    case SERVO_GET_ANGLE:
        if (copy_to_user((int __user *)arg, &servo_dev->current_angle, sizeof(servo_dev->current_angle)))
            return -EFAULT;
        break;
        
    default:
        pr_err("Unknown ioctl command: 0x%x\n", cmd);
        return -ENOTTY;
    }
    
    return ret;
}

static int servo_open(struct inode *inode, struct file *filp)
{
    pr_info("Servo device opened\n");
    return 0;
}

static int servo_release(struct inode *inode, struct file *filp)
{
    pr_info("Servo device closed\n");
    return 0;
}

static const struct file_operations servo_fops = {
    .owner = THIS_MODULE,
    .open = servo_open,
    .release = servo_release,
    .unlocked_ioctl = servo_ioctl,
};

static struct pwm_device *servo_get_pwm_device(void)
{
    struct pwm_device *pwm;
    int i;
    
    // 尝试不同的PWM设备名称
    const char *pwm_names[] = {
        "pwmchip2",         // 直接使用设备名
        "2030000.pwm",      // PWM3的设备地址
        "sg90",        // 自定义名称
        NULL
    };
    
    for (i = 0; pwm_names[i] != NULL; i++) {
        pwm = pwm_get(NULL, pwm_names[i]);
        if (!IS_ERR(pwm)) {
            pr_info("Found PWM device: %s\n", pwm_names[i]);
            return pwm;
        }
        pr_info("PWM device %s not available: %ld\n", pwm_names[i], PTR_ERR(pwm));
    }
    
    // 如果以上都失败，尝试通过索引获取
    pwm = pwm_get(NULL, NULL);
    if (!IS_ERR(pwm)) {
        pr_info("Got PWM device by index\n");
        return pwm;
    }
    
    return ERR_PTR(-ENODEV);
}

static int __init servo_init(void)
{
    int ret;
    
    // 分配设备结构
    servo_dev = kzalloc(sizeof(struct servo_device), GFP_KERNEL);
    if (!servo_dev)
        return -ENOMEM;
    
    // 获取PWM设备
    servo_dev->pwm = servo_get_pwm_device();
    if (IS_ERR(servo_dev->pwm)) {
        pr_err("Failed to get any PWM device\n");
        ret = PTR_ERR(servo_dev->pwm);
        goto err_pwm;
    }
    
    pr_info("Using PWM device successfully\n");
    
    // 分配设备号
    ret = alloc_chrdev_region(&servo_dev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("Failed to allocate device number\n");
        goto err_alloc;
    }
    
    // 初始化字符设备
    cdev_init(&servo_dev->cdev, &servo_fops);
    servo_dev->cdev.owner = THIS_MODULE;
    
    // 添加字符设备
    ret = cdev_add(&servo_dev->cdev, servo_dev->devno, 1);
    if (ret < 0) {
        pr_err("Failed to add cdev\n");
        goto err_cdev;
    }
    
    // 创建设备类
    servo_dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(servo_dev->class)) {
        pr_err("Failed to create device class\n");
        ret = PTR_ERR(servo_dev->class);
        goto err_class;
    }
    
    // 创建设备节点
    servo_dev->device = device_create(servo_dev->class, NULL, servo_dev->devno, NULL, DEVICE_NAME);
    if (IS_ERR(servo_dev->device)) {
        pr_err("Failed to create device\n");
        ret = PTR_ERR(servo_dev->device);
        goto err_device;
    }
    
    // 初始化舵机到中间位置
    servo_set_angle(90);
    
    pr_info("Servo driver initialized successfully\n");
    
    return 0;

err_device:
    class_destroy(servo_dev->class);
err_class:
    cdev_del(&servo_dev->cdev);
err_cdev:
    unregister_chrdev_region(servo_dev->devno, 1);
err_alloc:
    pwm_put(servo_dev->pwm);
err_pwm:
    kfree(servo_dev);
    return ret;
}

static void __exit servo_exit(void)
{
    // 关闭PWM输出
    pwm_disable(servo_dev->pwm);
    pwm_put(servo_dev->pwm);
    
    // 销毁设备
    device_destroy(servo_dev->class, servo_dev->devno);
    class_destroy(servo_dev->class);
    cdev_del(&servo_dev->cdev);
    unregister_chrdev_region(servo_dev->devno, 1);
    
    kfree(servo_dev);
    pr_info("Servo driver exited\n");
}

module_init(servo_init);
module_exit(servo_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Servo Motor Driver for IMX6ULL");
MODULE_VERSION("1.0");