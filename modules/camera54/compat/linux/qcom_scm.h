/* moved on 6.12, and the kernel's copy lacks the camera calls soc-repo exports */
#include <linux/firmware/qcom/qcom_scm.h>
extern int qcom_scm_smmu_notify_secure_lut(u64 dev_id, bool secure);
extern int qcom_scm_camera_protect_phy_lanes(bool protect, u64 regmask);
