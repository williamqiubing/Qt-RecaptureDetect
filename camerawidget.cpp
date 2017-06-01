#include "camerawidget.h"
#include "ui_camerawidget.h"

CameraWidget::CameraWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::CameraWidget)
{
    ui->setupUi(this);

    timer = new QTimer(this);
    image = new QImage();
    imgName = "lena.jpg";    // 测试使用
    registProcess = new QProcess(this);
    recognizeProcess = new QProcess(this);


    // 信号槽
    connect(timer, SIGNAL(timeout()), this, SLOT(readFrame()));  // 时间到，读取摄像头信息
    connect(ui->button_openCam, SIGNAL(clicked()), this, SLOT(openCamera()));
    connect(ui->button_takPic, SIGNAL(clicked()), this, SLOT(takingPicture()));
    connect(ui->button_closeCam, SIGNAL(clicked()), this, SLOT(closeCamera()));
    connect(ui->button_regist, SIGNAL(clicked()), this, SLOT(regist()));
    connect(ui->button_detect, SIGNAL(clicked()), this, SLOT(detect()));
}


// 打开摄像头
void CameraWidget::openCamera()
{
    // 获取摄像头视频
    if(capture.isOpened())
    {
        LOG(WARNING) << "Camera is already opened and is will released!" << endl;
        capture.release();
    }
    //capture.open("http://192.168.23.2:8888/video?dummy=param.mjpg");
    capture.open(0);

    if(!capture.isOpened())
    {
        LOG(ERROR) << "Open camera failed!" << endl;
        return;
    }

    LOG(INFO) << "Open camera successfully!\n";
    // 初始化Mat的大小
    frame = Mat::zeros(capture.get(CV_CAP_PROP_FRAME_HEIGHT),
                          capture.get(CV_CAP_PROP_FRAME_WIDTH),
                          CV_8UC3);

    // camera params
    LOG(INFO) << "Camera size : "
              << capture.get(CV_CAP_PROP_FRAME_HEIGHT)
              << " * "
              << capture.get(CV_CAP_PROP_FRAME_WIDTH)
              << endl;
    LOG(INFO) << "Camera frame rate: "
              << capture.get(CV_CAP_PROP_FPS)
              << endl;

    // 开始计时，超时则发出timeout()信号
    timer->start(30);
}


// 读取摄像头信息
void CameraWidget::readFrame()
{
    // 从摄像头中获取一帧图像
    capture >> frame;

    if(frame.data)
    {
        // 将抓取到的帧，转换为QImage格式。QImage::Format_RGB888不同的摄像头用不同的格式。
        QImage qImage = QImage((const uchar*)frame.data,
                               frame.cols, frame.rows,
                               QImage::Format_RGB888).rgbSwapped();
        // 将图片显示到label上
        ui->label_cam->setPixmap(QPixmap::fromImage(qImage));
    }
    else
    {
        LOG(WARNING) << "Read camera data failed!" << endl;
    }
}


// 拍照
void CameraWidget::takingPicture()
{
    if(!capture.isOpened())
    {
        LOG(WARNING) << "Camera is not opened failed!";
        return;
    }

    capture >> frame;
    Mat dstImage = frame.clone();

    // 保存图片
    imgName = getPicNameString();
    cv::imwrite("images\\" + imgName.toStdString(), dstImage);
    LOG(INFO) << "Take picture successfully. Save image in: "
              << "images\\" + imgName.toStdString();

    // 将抓取到的帧，转换为QImage格式。QImage::Format_RGB888不同的摄像头用不同的格式。
    QImage qImage = QImage((const uchar*)frame.data,
                           frame.cols, frame.rows,
                           QImage::Format_RGB888).rgbSwapped();
    // 将图片显示到label上
    ui->label_pic->setPixmap(QPixmap::fromImage(qImage));
}


// 关闭摄像头
void CameraWidget::closeCamera()
{
    timer->stop();         // 停止读取数据
    LOG(INFO) << "Camera is released"
              << endl;
    capture.release();    // 释放摄像头
}


void CameraWidget::regist()
{
    LOG(INFO) << "Click register button";
    QString program = "Register.exe";
    QStringList arguments;
    arguments << "-0" << "images\\" + imgName;
    connect(registProcess,
            SIGNAL(readyRead()),
            this,
            SLOT(readRegisterResult()));

    // 执行注册程序
    registProcess->start(program, arguments);
    registProcess->waitForStarted();
    LOG(INFO) << "Register picture: "
              << "images\\" + imgName.toStdString();
    LOG(INFO) << QString::fromLocal8Bit(registProcess->readAll()).toStdString();
}


void CameraWidget::readRegisterResult()
{
    QProcess *pProces = (QProcess *)sender();
    QString result = pProces->readAll();

    // 注册成功返回信息正则表达式
    QString successStr("Register successfully, ID: (.*)");
    QRegExp successRegExp(successStr);
    QString failStr("Faces are not detected.");
    QRegExp failRegExp(failStr);


    // 将执行结果用提示框展示
    if(result.indexOf(successRegExp) >= 0)
    {
        QString id = successRegExp.cap(1);
        QMessageBox::information(NULL,
                                 "注册提示",
                                 "恭喜你，注册成功 ! ID: " + id);
        LOG(INFO) << "Register successfully and ID: "
                  << id.toStdString()
                  << endl;
    }
    else if(result.indexOf(failRegExp) >= 0)
    {
        QMessageBox::warning(NULL,
                             "注册提示",
                             "注册失败，未检测到人脸 !");
        // 注册失败需要关闭程序
        registProcess->close();

        // imgName = "1.jpg";
        LOG(WARNING) << "Register failed, the register program was killed and exited.";

    }
    else
    {
        LOG(WARNING) << "Register stdout : " << result.toStdString();
    }

    LOG(INFO) << "Show messagebox ok !";
}


void CameraWidget::detect()
{
    LOG(INFO) << "Click detect button";

    if (!recaptureInitialize())
    {
        LOG(ERROR) << "recaptureInitialize called failed!"
                   << endl;
        return;
    }

    // Debug 目录
    // qDebug() << "app path : " <<  QCoreApplication::applicationDirPath();
    // build 目录
    LOG(INFO) << "current path: " << QDir::currentPath().toStdString();

    // 调用Matlab编译出的翻拍检测动态库
    const QString NATURE_MAT = "testna5190.mat";
    const QString RECAPTURE_MAT = "testre5190.mat";
    QString imagePath = QDir::currentPath() + "/images/" + imgName;
    QString nature_mat = QDir::currentPath() + "/mats/" + NATURE_MAT;
    QString recapture_mat = QDir::currentPath() + "/mats/" + RECAPTURE_MAT;
    int result = 0;

    mwArray ImagePath(imagePath.toStdString().c_str());
    mwArray NaPath(nature_mat.toStdString().c_str());
    mwArray RePath(recapture_mat.toStdString().c_str());
    mwArray Result(result);

    LOG(INFO) << "Start detect recapture for : " << imgName.toStdString();
    main_FeatureClassifier(1, Result, ImagePath, NaPath, RePath);
    std::stringstream oss;
    oss << Result;
    if(oss.str() == "1")
    {
        LOG(INFO) << "It's natural picture and will start face recognize."
                  << endl;
        QString program = "Register.exe";
        QStringList arguments;
        arguments << "-1" << "images\\" + imgName;
        connect(recognizeProcess,
                SIGNAL(readyRead()),
                this,
                SLOT(readRecognizeResult()));

        // 执行注册程序
        recognizeProcess->start(program, arguments);
        recognizeProcess->waitForStarted();
        LOG(INFO) << QString::fromLocal8Bit(recognizeProcess->readAll()).toStdString();
    }
    else
    {
        LOG(INFO) << "It's recapture pciture."
                  << endl;
        QMessageBox::warning(NULL,
                             "识别信息",
                             "该图片是翻拍 !");
    }
}


void CameraWidget::readRecognizeResult()
{
    QProcess *pProces = (QProcess *)sender();
    QString result = pProces->readAll();

    // 注册成功返回信息正则表达式
    QString successStr("ID: (.*)\r\n");
    QRegExp successRegExp(successStr);
    QString failStr("Faces are not detected.");
    QRegExp failRegExp(failStr);

    // 将执行结果用提示框展示
    if(result.indexOf(successRegExp) >= 0)
    {
        QString id = successRegExp.cap(1);
        QMessageBox::information(NULL,
                                 "识别信息",
                                 "识别成功，ID: " + id);
        LOG(INFO) << "Face recognize successfully, ID: "
                  << id.toStdString()
                  << endl;
    }
    else if(result.indexOf(failRegExp) >= 0)
    {
        QMessageBox::warning(NULL,
                             "识别信息",
                             "识别错误，未检测到人脸 !");
        // 注册失败需要关闭程序
        registProcess->close();
        LOG(WARNING) << "Face are not detected, the Register.exe was killed and exited.";
    }
    else
    {
        LOG(WARNING) << "Recognize stdout : " << result.toStdString();
    }
    LOG(INFO) << "Show messagebox ok !";
}


QString CameraWidget::getPicNameString()
{
    QDateTime time = QDateTime::currentDateTime();
    QString name = time.toString("yyyy-MM-dd_hh-mm-ss") + ".jpg";
    return name;
}


CameraWidget::~CameraWidget()
{
    delete ui;
}
