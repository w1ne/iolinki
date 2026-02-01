Dear fellow IO-Link Device designer,

this package contains 

- IOL-Interface-Spec_10002_V1.1.1.5_Oct2025.pdf
	Final specification

- IOL-Interface-Spec_10002_M1.1.1.5_Oct2025.pdf
	Marked version of final specification with all changes marked in yellow and CRs attached

The validity period of packages and associated standards is provided to the public via publications of the "Specification Validity Periods" document, available on 
	www.IO-Link.com/downloads, section "Specification".

Please read the change log at the bottom very careful to get an overview of the changed aspects

Please provide any issue as a change request at www.io-link-projects.com by providing name and email address with the 
login: IO-Link-V113 
password: Report

Your participation ensures the success of this community, thank you

	The IO-Link core team

Change log:
_______________________________________________________________________________________________________________
IO-Link Interface and System Specification V1.1.5:

# accepted CRs from 357 to 411

# clarified behavior for Back-to-box command
# removed tIdle definition
# clarified TimeSpanT definition
# clarified behavior during block transfer
# editorial corrections on references and descriptions

_______________________________________________________________________________________________________________
IO-Link Interface and System Specification V1.1.4:

# accepted CRs from 214 to 373

# optimized EMC standard definitions for specific cases
# enhanced master behavior in case of absent devices at startup
# optimized device parameter and data storage manager state machine
# optimized description of event timing restrictions
# corrected m-sequence table A.9
# optimized behavior for incorrect timing assignments
# restriction added for write-only parameters
# added definition for incorrect ds_data content
# optimized revision check at communication startup
# optimized port power status readback
# clarification on question in cycle time corner cases
# added new identification parameter ProductURI
# optimized domains for argblockids
# clarification for process data qualifier for corner cases
# optimization for block transfer restrictions
# optimized pin5 definition for devices
# added dependency description between device status and detailed device status
# optimized device status description in respect to device behavior
# extended description of parameter type for reset commands
# optimized behavior description for communication restart after reset
# including comments from IEC 61331-9 review
# adapted description of direct parameter page content after deviceid switch including baudrate switch
# clarification on reinvocation of portpoweroff
# allowing vendor specific port status in smi
# added smi error type for incorrect configuration
# editorial corrections on references and descriptions


