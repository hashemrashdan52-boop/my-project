; *** Inno Setup version 6.1.0+ Arabic messages (written for HR Premiere) ***
;
; Messages that are not translated here fall back to the English text of Default.isl.
; The file is UTF-8 with a byte-order mark, as Inno Setup 6 expects for Unicode language files.

[LangOptions]
; <0627><0644><0639><0631><0628><064A><0629> = العربية
LanguageName=<0627><0644><0639><0631><0628><064A><0629>
LanguageID=$0401
LanguageCodePage=0
RightToLeft=yes

[Messages]

; *** Application titles
SetupAppTitle=معالج التثبيت
SetupWindowTitle=تثبيت - %1
UninstallAppTitle=إزالة التثبيت
UninstallAppFullTitle=إزالة تثبيت %1

; *** Misc. common
InformationTitle=معلومات
ConfirmTitle=تأكيد
ErrorTitle=خطأ

; *** SetupLdr messages
SetupLdrStartupMessage=سيتم الآن تثبيت %1. هل تريد المتابعة؟
LdrCannotCreateTemp=تعذّر إنشاء ملف مؤقت. تم إلغاء التثبيت
LdrCannotExecTemp=تعذّر تشغيل ملف في المجلد المؤقت. تم إلغاء التثبيت

; *** Startup error messages
LastErrorMessage=%1.%n%nخطأ %2: %3
SetupFileMissing=الملف %1 غير موجود في مجلد التثبيت. يرجى حل المشكلة أو الحصول على نسخة جديدة من البرنامج.
SetupFileCorrupt=ملفات التثبيت تالفة. يرجى الحصول على نسخة جديدة من البرنامج.
SetupFileCorruptOrWrongVer=ملفات التثبيت تالفة أو غير متوافقة مع هذا الإصدار من المعالج. يرجى حل المشكلة أو الحصول على نسخة جديدة من البرنامج.
InvalidParameter=تم تمرير معامل غير صالح في سطر الأوامر:%n%n%1
SetupAlreadyRunning=معالج التثبيت قيد التشغيل بالفعل.
WindowsVersionNotSupported=هذا البرنامج لا يدعم إصدار ويندوز المثبّت على جهازك.
WindowsServicePackRequired=يتطلب هذا البرنامج %1 مع الحزمة الخدمية %2 أو أحدث.
NotOnThisPlatform=هذا البرنامج لا يعمل على %1.
OnlyOnThisPlatform=يجب تشغيل هذا البرنامج على %1.
OnlyOnTheseArchitectures=يمكن تثبيت هذا البرنامج فقط على إصدارات ويندوز المصمّمة لمعمارية المعالجات التالية:%n%n%1
WinVersionTooLowError=يتطلب هذا البرنامج %1 الإصدار %2 أو أحدث.
WinVersionTooHighError=لا يمكن تثبيت هذا البرنامج على %1 الإصدار %2 أو أحدث.
AdminPrivilegesRequired=يجب تسجيل الدخول كمسؤول عند تثبيت هذا البرنامج.
PowerUserPrivilegesRequired=يجب تسجيل الدخول كمسؤول أو كعضو في مجموعة Power Users عند تثبيت هذا البرنامج.
SetupAppRunningError=اكتشف المعالج أن %1 قيد التشغيل حالياً.%n%nأغلق كل نسخ البرنامج ثم اضغط «موافق» للمتابعة أو «إلغاء» للخروج.
UninstallAppRunningError=اكتشف معالج الإزالة أن %1 قيد التشغيل حالياً.%n%nأغلق كل نسخ البرنامج ثم اضغط «موافق» للمتابعة أو «إلغاء» للخروج.

; *** Startup questions
PrivilegesRequiredOverrideTitle=اختيار وضع التثبيت
PrivilegesRequiredOverrideInstruction=اختر وضع التثبيت
PrivilegesRequiredOverrideText1=يمكن تثبيت %1 لجميع المستخدمين (يتطلب صلاحيات المسؤول) أو لك وحدك.
PrivilegesRequiredOverrideText2=يمكن تثبيت %1 لك وحدك أو لجميع المستخدمين (يتطلب صلاحيات المسؤول).
PrivilegesRequiredOverrideAllUsers=تثبيت لجميع المستخدمين
PrivilegesRequiredOverrideAllUsersRecommended=تثبيت لجميع المستخدمين (موصى به)
PrivilegesRequiredOverrideCurrentUser=تثبيت لي وحدي
PrivilegesRequiredOverrideCurrentUserRecommended=تثبيت لي وحدي (موصى به)

; *** Misc. errors
ErrorCreatingDir=تعذّر على المعالج إنشاء المجلد "%1"
ErrorTooManyFilesInDir=تعذّر إنشاء ملف في المجلد "%1" لأنه يحتوي على عدد كبير جداً من الملفات

; *** Setup common messages
ExitSetupTitle=الخروج من التثبيت
ExitSetupMessage=لم يكتمل التثبيت. إذا خرجت الآن فلن يتم تثبيت البرنامج.%n%nيمكنك تشغيل المعالج مرة أخرى في وقت لاحق لإكمال التثبيت.%n%nهل تريد الخروج؟
AboutSetupMenuItem=&حول المعالج...
AboutSetupTitle=حول المعالج
AboutSetupMessage=%1 الإصدار %2%n%3%n%n%1 الصفحة الرئيسية:%n%4
AboutSetupNote=
TranslatorNote=

; *** Buttons
ButtonBack=السابق
ButtonNext=التالي
ButtonInstall=تثبيت
ButtonOK=موافق
ButtonCancel=إلغاء
ButtonYes=نعم
ButtonYesToAll=نعم للكل
ButtonNo=لا
ButtonNoToAll=لا للكل
ButtonFinish=إنهاء
ButtonBrowse=استعراض...
ButtonWizardBrowse=استعراض...
ButtonNewFolder=إنشاء مجلد جديد

; *** "Select Language" dialog messages
SelectLanguageTitle=اختيار لغة التثبيت
SelectLanguageLabel=اختر اللغة التي سيُستخدم بها المعالج أثناء التثبيت.

; *** Common wizard text
ClickNext=اضغط «التالي» للمتابعة أو «إلغاء» للخروج من التثبيت.
BeveledLabel=
BrowseDialogTitle=استعراض المجلدات
BrowseDialogLabel=اختر مجلداً من القائمة ثم اضغط «موافق».
NewFolderName=مجلد جديد

; *** "Welcome" wizard page
WelcomeLabel1=مرحباً بك في معالج تثبيت [name]
WelcomeLabel2=سيقوم هذا المعالج بتثبيت [name/ver] على جهازك.%n%nيُنصح بإغلاق جميع التطبيقات الأخرى قبل المتابعة.

; *** "Password" wizard page
WizardPassword=كلمة المرور
PasswordLabel1=هذا التثبيت محمي بكلمة مرور.
PasswordLabel3=أدخل كلمة المرور ثم اضغط «التالي». كلمات المرور حساسة لحالة الأحرف.
PasswordEditLabel=كلمة المرور:
IncorrectPassword=كلمة المرور التي أدخلتها غير صحيحة. حاول مرة أخرى.

; *** "License Agreement" wizard page
WizardLicense=اتفاقية الترخيص
LicenseLabel=يرجى قراءة المعلومات المهمة التالية قبل المتابعة.
LicenseLabel3=يرجى قراءة اتفاقية الترخيص التالية. يجب أن توافق على شروطها قبل متابعة التثبيت.
LicenseAccepted=أنا أوافق على الاتفاقية
LicenseNotAccepted=أنا لا أوافق على الاتفاقية

; *** "Information" wizard pages
WizardInfoBefore=معلومات
InfoBeforeLabel=يرجى قراءة المعلومات المهمة التالية قبل المتابعة.
InfoBeforeClickLabel=عندما تكون جاهزاً لمتابعة التثبيت اضغط «التالي».
WizardInfoAfter=معلومات
InfoAfterLabel=يرجى قراءة المعلومات المهمة التالية قبل المتابعة.
InfoAfterClickLabel=عندما تكون جاهزاً لمتابعة التثبيت اضغط «التالي».

; *** "User Information" wizard page
WizardUserInfo=معلومات المستخدم
UserInfoDesc=يرجى إدخال معلوماتك.
UserInfoName=اسم المستخدم:
UserInfoOrg=المؤسسة:
UserInfoSerial=الرقم التسلسلي:
UserInfoNameRequired=يجب إدخال اسم.

; *** "Select Destination Location" wizard page
WizardSelectDir=اختيار مكان التثبيت
SelectDirDesc=أين تريد تثبيت [name]؟
SelectDirLabel3=سيقوم المعالج بتثبيت [name] في المجلد التالي.
SelectDirBrowseLabel=للمتابعة اضغط «التالي». وإذا أردت اختيار مجلد آخر اضغط «استعراض».
DiskSpaceGBLabel=يلزم توفر [gb] جيجابايت على الأقل من المساحة الحرة.
DiskSpaceMBLabel=يلزم توفر [mb] ميجابايت على الأقل من المساحة الحرة.
CannotInstallToNetworkDrive=لا يمكن التثبيت على قرص شبكة.
CannotInstallToUNCPath=لا يمكن التثبيت على مسار UNC.
InvalidPath=يجب إدخال مسار كامل مع حرف القرص، مثال:%n%nC:\APP%n%nأو مسار UNC بالشكل:%n%n\\server\share
InvalidDrive=القرص أو المشاركة التي اخترتها غير موجودة أو لا يمكن الوصول إليها. اختر غيرها.
DiskSpaceWarningTitle=المساحة الحرة غير كافية
DiskSpaceWarning=يحتاج التثبيت إلى %1 كيلوبايت على الأقل من المساحة الحرة، لكن القرص المختار فيه %2 كيلوبايت فقط.%n%nهل تريد المتابعة رغم ذلك؟
DirNameTooLong=اسم المجلد أو مساره طويل جداً.
InvalidDirName=اسم المجلد غير صالح.
BadDirName32=لا يجوز أن يحتوي اسم المجلد على أي من الأحرف التالية:%n%n%1
DirExistsTitle=المجلد موجود
DirExists=المجلد:%n%n%1%n%nموجود بالفعل. هل تريد التثبيت فيه رغم ذلك؟
DirDoesntExistTitle=المجلد غير موجود
DirDoesntExist=المجلد:%n%n%1%n%nغير موجود. هل تريد إنشاءه؟

; *** "Select Components" wizard page
WizardSelectComponents=اختيار المكوّنات
SelectComponentsDesc=أي المكوّنات تريد تثبيتها؟
SelectComponentsLabel2=اختر المكوّنات التي تريد تثبيتها وألغِ تحديد التي لا تريدها. اضغط «التالي» عندما تكون جاهزاً للمتابعة.
FullInstallation=تثبيت كامل
CompactInstallation=تثبيت مضغوط
CustomInstallation=تثبيت مخصص
NoUninstallWarningTitle=المكوّنات موجودة
NoUninstallWarning=اكتشف المعالج أن المكوّنات التالية مثبّتة بالفعل على جهازك:%n%n%1%n%nإلغاء تحديدها لن يزيلها.%n%nهل تريد المتابعة؟
ComponentSize1=%1 كيلوبايت
ComponentSize2=%1 ميجابايت
ComponentsDiskSpaceGBLabel=يتطلب الاختيار الحالي [gb] جيجابايت على الأقل من المساحة الحرة.
ComponentsDiskSpaceMBLabel=يتطلب الاختيار الحالي [mb] ميجابايت على الأقل من المساحة الحرة.

; *** "Select Additional Tasks" wizard page
WizardSelectTasks=اختيار مهام إضافية
SelectTasksDesc=أي مهام إضافية تريد تنفيذها؟
SelectTasksLabel2=اختر المهام الإضافية التي تريد أن ينفذها المعالج أثناء تثبيت [name] ثم اضغط «التالي».

; *** "Select Start Menu Folder" wizard page
WizardSelectProgramGroup=اختيار مجلد قائمة ابدأ
SelectStartMenuFolderDesc=أين تريد أن يضع المعالج اختصارات البرنامج؟
SelectStartMenuFolderLabel3=سينشئ المعالج اختصارات البرنامج في مجلد قائمة «ابدأ» التالي.
SelectStartMenuFolderBrowseLabel=للمتابعة اضغط «التالي». وإذا أردت اختيار مجلد آخر اضغط «استعراض».
MustEnterGroupName=يجب إدخال اسم المجلد.
GroupNameTooLong=اسم المجلد أو مساره طويل جداً.
InvalidGroupName=اسم المجلد غير صالح.
BadGroupName=لا يجوز أن يحتوي اسم المجلد على أي من الأحرف التالية:%n%n%1
NoProgramGroupCheck2=عدم إنشاء مجلد في قائمة «ابدأ»

; *** "Ready to Install" wizard page
WizardReady=جاهز للتثبيت
ReadyLabel1=المعالج جاهز الآن لبدء تثبيت [name] على جهازك.
ReadyLabel2a=اضغط «تثبيت» لمتابعة التثبيت، أو «السابق» إذا أردت مراجعة الإعدادات أو تغييرها.
ReadyLabel2b=اضغط «تثبيت» لمتابعة التثبيت.
ReadyMemoUserInfo=معلومات المستخدم:
ReadyMemoDir=مكان التثبيت:
ReadyMemoType=نوع التثبيت:
ReadyMemoComponents=المكوّنات المختارة:
ReadyMemoGroup=مجلد قائمة «ابدأ»:
ReadyMemoTasks=مهام إضافية:

; *** "Preparing to Install" wizard page
WizardPreparing=التحضير للتثبيت
PreparingDesc=يحضّر المعالج لتثبيت [name] على جهازك.
PreviousInstallNotCompleted=لم يكتمل تثبيت أو إزالة برنامج سابق. يجب إعادة تشغيل الجهاز لإكمال ذلك.%n%nبعد إعادة التشغيل شغّل المعالج مرة أخرى لإكمال تثبيت [name].
CannotContinue=لا يمكن للمعالج المتابعة. اضغط «إلغاء» للخروج.
ApplicationsFound=التطبيقات التالية تستخدم ملفات يحتاج المعالج إلى تحديثها. يُنصح بالسماح للمعالج بإغلاقها تلقائياً.
ApplicationsFound2=التطبيقات التالية تستخدم ملفات يحتاج المعالج إلى تحديثها. يُنصح بالسماح للمعالج بإغلاقها تلقائياً. بعد انتهاء التثبيت سيحاول المعالج إعادة تشغيلها.
CloseApplications=إغلاق التطبيقات تلقائياً
DontCloseApplications=عدم إغلاق التطبيقات
ErrorCloseApplications=تعذّر على المعالج إغلاق كل التطبيقات تلقائياً. يُنصح بإغلاق كل التطبيقات التي تستخدم ملفات يحتاج المعالج إلى تحديثها قبل المتابعة.
PrepareToInstallNeedsRestart=يجب أن يعيد المعالج تشغيل جهازك. بعد إعادة التشغيل شغّل المعالج مرة أخرى لإكمال تثبيت [name].%n%nهل تريد إعادة التشغيل الآن؟

; *** "Installing" wizard page
WizardInstalling=جارٍ التثبيت
InstallingLabel=يرجى الانتظار بينما يثبّت المعالج [name] على جهازك.

; *** "Setup Completed" wizard page
FinishedHeadingLabel=اكتمل تثبيت [name]
FinishedLabelNoIcons=انتهى المعالج من تثبيت [name] على جهازك.
FinishedLabel=انتهى المعالج من تثبيت [name] على جهازك. يمكنك تشغيل البرنامج من الاختصارات التي تم إنشاؤها.
ClickFinish=اضغط «إنهاء» للخروج من المعالج.
FinishedRestartLabel=لإكمال تثبيت [name] يجب أن يعيد المعالج تشغيل جهازك. هل تريد إعادة التشغيل الآن؟
FinishedRestartMessage=لإكمال تثبيت [name] يجب أن يعيد المعالج تشغيل جهازك.%n%nهل تريد إعادة التشغيل الآن؟
ShowReadmeCheck=نعم، أريد عرض ملف «اقرأني»
YesRadio=نعم، أعد تشغيل الجهاز الآن
NoRadio=لا، سأعيد تشغيل الجهاز لاحقاً
RunEntryExec=تشغيل %1
RunEntryShellExec=عرض %1

; *** Installation phase messages
SetupAborted=لم يكتمل التثبيت.%n%nيرجى حل المشكلة وتشغيل المعالج مرة أخرى.
AbortRetryIgnoreSelectAction=اختر إجراءً
AbortRetryIgnoreRetry=إعادة المحاولة
AbortRetryIgnoreIgnore=تجاهل الخطأ والمتابعة
AbortRetryIgnoreCancel=إلغاء التثبيت

; *** Status messages
StatusClosingApplications=جارٍ إغلاق التطبيقات...
StatusCreateDirs=جارٍ إنشاء المجلدات...
StatusExtractFiles=جارٍ استخراج الملفات...
StatusCreateIcons=جارٍ إنشاء الاختصارات...
StatusCreateIniEntries=جارٍ إنشاء مدخلات INI...
StatusCreateRegistryEntries=جارٍ إنشاء مدخلات السجل...
StatusRegisterFiles=جارٍ تسجيل الملفات...
StatusSavingUninstall=جارٍ حفظ معلومات الإزالة...
StatusRunProgram=جارٍ إنهاء التثبيت...
StatusRestartingApplications=جارٍ إعادة تشغيل التطبيقات...
StatusRollback=جارٍ التراجع عن التغييرات...

; *** Misc. errors
ErrorInternal2=خطأ داخلي: %1
ErrorFunctionFailedNoCode=فشلت العملية %1
ErrorFunctionFailed=فشلت العملية %1؛ الرمز %2
ErrorFunctionFailedWithMessage=فشلت العملية %1؛ الرمز %2.%n%3
ErrorExecutingProgram=تعذّر تشغيل الملف:%n%1

; *** Registry errors
ErrorRegOpenKey=خطأ في فتح مفتاح السجل:%n%1\%2
ErrorRegCreateKey=خطأ في إنشاء مفتاح السجل:%n%1\%2
ErrorRegWriteKey=خطأ في الكتابة إلى مفتاح السجل:%n%1\%2

; *** INI errors
ErrorIniEntry=خطأ في إنشاء مدخل INI في الملف "%1".

; *** File copying errors
FileAbortRetryIgnoreSkipNotRecommended=تخطي هذا الملف (غير موصى به)
FileAbortRetryIgnoreIgnoreNotRecommended=تجاهل الخطأ والمتابعة (غير موصى به)
SourceIsCorrupted=الملف المصدر تالف
SourceDoesntExist=الملف المصدر "%1" غير موجود
ExistingFileReadOnly2=تعذّر استبدال الملف الموجود لأنه للقراءة فقط.
ExistingFileReadOnlyRetry=إزالة خاصية القراءة فقط وإعادة المحاولة
ExistingFileReadOnlyKeepExisting=الإبقاء على الملف الموجود
ErrorReadingExistingDest=حدث خطأ أثناء محاولة قراءة الملف الموجود:
FileExistsSelectAction=اختر إجراءً
FileExists2=الملف موجود بالفعل.
FileExistsOverwriteExisting=استبدال الملف الموجود
FileExistsKeepExisting=الإبقاء على الملف الموجود
FileExistsOverwriteOrKeepAll=تنفيذ هذا الاختيار على الحالات التالية
ExistingFileNewerSelectAction=اختر إجراءً
ExistingFileNewer2=الملف الموجود أحدث من الملف الذي سيثبّته المعالج.
ExistingFileNewerOverwriteExisting=استبدال الملف الموجود
ExistingFileNewerKeepExisting=الإبقاء على الملف الموجود (موصى به)
ExistingFileNewerOverwriteOrKeepAll=تنفيذ هذا الاختيار على الحالات التالية
ErrorChangingAttr=حدث خطأ أثناء محاولة تغيير خصائص الملف الموجود:
ErrorCreatingTemp=حدث خطأ أثناء محاولة إنشاء ملف في مجلد الوجهة:
ErrorReadingSource=حدث خطأ أثناء محاولة قراءة الملف المصدر:
ErrorCopying=حدث خطأ أثناء محاولة نسخ الملف:
ErrorReplacingExistingFile=حدث خطأ أثناء محاولة استبدال الملف الموجود:
ErrorRestartReplace=فشلت عملية الاستبدال بعد إعادة التشغيل:
ErrorRenamingTemp=حدث خطأ أثناء محاولة إعادة تسمية ملف في مجلد الوجهة:
ErrorRegisterServer=تعذّر تسجيل مكتبة DLL/OCX: %1
ErrorRegSvr32Failed=فشل RegSvr32 مع رمز الخروج %1
ErrorRegisterTypeLib=تعذّر تسجيل مكتبة الأنواع: %1

; *** Uninstall display name markings
UninstallDisplayNameMark=%1 (%2)
UninstallDisplayNameMarks=%1 (%2، %3)
UninstallDisplayNameMark32Bit=32 بت
UninstallDisplayNameMark64Bit=64 بت
UninstallDisplayNameMarkAllUsers=جميع المستخدمين
UninstallDisplayNameMarkCurrentUser=المستخدم الحالي

; *** Post-installation errors
ErrorOpeningReadme=حدث خطأ أثناء محاولة فتح ملف «اقرأني».
ErrorRestartingComputer=تعذّر على المعالج إعادة تشغيل الجهاز. يرجى فعل ذلك يدوياً.

; *** Uninstaller messages
UninstallNotFound=الملف "%1" غير موجود. لا يمكن الإزالة.
UninstallOpenError=تعذّر فتح الملف "%1". لا يمكن الإزالة
UninstallUnsupportedVer=ملف سجل الإزالة "%1" بصيغة لا يتعرف عليها هذا الإصدار من معالج الإزالة. لا يمكن الإزالة
UninstallUnknownEntry=تم العثور على مدخل غير معروف (%1) في سجل الإزالة
ConfirmUninstall=هل أنت متأكد أنك تريد إزالة %1 وجميع مكوّناته بالكامل؟
UninstallOnlyOnWin64=يمكن إزالة هذا التثبيت فقط على ويندوز 64 بت.
OnlyAdminCanUninstall=يمكن إزالة هذا التثبيت فقط بواسطة مستخدم لديه صلاحيات المسؤول.
UninstallStatusLabel=يرجى الانتظار بينما تتم إزالة %1 من جهازك.
UninstalledAll=تمت إزالة %1 من جهازك بنجاح.
UninstalledMost=اكتملت إزالة %1.%n%nتعذّرت إزالة بعض العناصر. يمكنك إزالتها يدوياً.
UninstalledAndNeedsRestart=لإكمال إزالة %1 يجب إعادة تشغيل جهازك.%n%nهل تريد إعادة التشغيل الآن؟
UninstallDataCorrupted=الملف "%1" تالف. لا يمكن الإزالة

; *** Uninstallation phase messages
ConfirmDeleteSharedFileTitle=إزالة ملف مشترك؟
ConfirmDeleteSharedFile2=يشير النظام إلى أن الملف المشترك التالي لم يعد مستخدماً من أي برنامج. هل تريد أن يزيله معالج الإزالة؟%n%nإذا كانت هناك برامج ما زالت تستخدم هذا الملف فقد لا تعمل بشكل صحيح بعد إزالته. إذا لم تكن متأكداً اختر «لا». إبقاء الملف على جهازك لا يسبب أي ضرر.
SharedFileNameLabel=اسم الملف:
SharedFileLocationLabel=المكان:
WizardUninstalling=حالة الإزالة
StatusUninstalling=جارٍ إزالة %1...

; *** Shutdown block reasons
ShutdownBlockReasonInstallingApp=جارٍ تثبيت %1.
ShutdownBlockReasonUninstallingApp=جارٍ إزالة %1.

[CustomMessages]
NameAndVersion=%1 الإصدار %2
AdditionalIcons=اختصارات إضافية:
CreateDesktopIcon=إنشاء اختصار على سطح المكتب
CreateQuickLaunchIcon=إنشاء اختصار في شريط التشغيل السريع
ProgramOnTheWeb=%1 على الإنترنت
UninstallProgram=إزالة %1
LaunchProgram=تشغيل %1
AssocFileExtension=ربط %1 بامتداد الملفات %2
AssocingFileExtension=جارٍ ربط %1 بامتداد الملفات %2...
AutoStartProgramGroupDescription=بدء التشغيل:
AutoStartProgram=تشغيل %1 تلقائياً
AddonHostProgramNotFound=تعذّر العثور على %1 في المجلد الذي اخترته.%n%nهل تريد المتابعة رغم ذلك؟
