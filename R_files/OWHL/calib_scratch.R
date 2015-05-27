# calib_scratch.R
# A script to look at the calibration data from 2015-05-15, the last day of
# the prototype deployment of the OWHL sensor at HMS. 
# Author: Luke Miller May 26, 2015
###############################################################################


myf = "D:/Miller_projects/Electronics/OWHL/20150326_Field_test/OWHL_calib_20150515.csv"

df = read.csv(myf)

df$DateTime = as.POSIXct(df$DateTime, origin = '1970-1-1',tz = '')

fresh = df[df$Water == 'fresh',]
salt = df[df$Water == 'salt',]

# Initial height of water, use as baseline. 
freshinit = 30.0 # centimeters of water
saltinit = 20.25 # centimeters of saltwater

# Density of water
freshdens = 1000 # kg per m^3
saltdens = 1026 # kg per m^3

# Pressure exerted by 1 cm of water
# pressure (Pascals) = height(m) * density * gravity, 	1 kg / m*s^2 = 1 Pa
freshPa = 0.01 * freshdens * 9.81
saltPa = 0.01 * saltdens * 9.81
# Convert from Pascals to mbar
# 1 Pa = 0.00001 bar = 0.01 mbar
freshmbar = freshPa * 0.01 # pressure increase due to 1 cm of water
saltmbar = saltPa * 0.01 # pressure increase due to 1 cm of salt water

# Subtract off the initial depth of water so that all increases are relative
# to the initial pressure. 
df$RelDepth.cm[df$Water=='fresh'] = df$Depth.cm[df$Water=='fresh'] - freshinit
df$RelDepth.cm[df$Water=='salt'] = df$Depth.cm[df$Water=='salt'] - saltinit

# Subtract off the initial pressure reading so that all pressure values are
# relative to the initial pressure
freshPress1 = mean(df$Pressure.mbar[df$Water=='fresh' & 
						df$Depth.cm == freshinit])
saltPress1 = mean(df$Pressure.mbar[df$Water=='salt' & 
						df$Depth.cm == saltinit])
df$RelPres.mbar[df$Water=='fresh'] = df$Pressure.mbar[df$Water=='fresh'] - 
		freshPress1
df$RelPres.mbar[df$Water=='salt'] = df$Pressure.mbar[df$Water=='salt'] - 
		saltPress1

# Use the relative depth value to calculate an estimated pressure reading
# for each depth value. 
df$EstPres.mbar[df$Water=='fresh'] = df$RelDepth.cm[df$Water=='fresh'] * 
		freshmbar
df$EstPres.mbar[df$Water=='salt'] = df$RelDepth.cm[df$Water=='salt'] * 
		saltmbar

##########################################################
#Plot the freshwater data
ylims = range(df$EstPres.mbar)
xlims = range(df$RelDepth.cm)
plot(RelPres.mbar~RelDepth.cm, data = df[df$Water=='fresh',], type = 'l',las=1,
		ylim = ylims,
		xlim = xlims,
		col = 'blue',
		lwd = 2,
		xlab = 'Height of water in tube (cm)',
		ylab = 'Pressure due to water in tube (mbar)')
lines(EstPres.mbar~RelDepth.cm, data = df[df$Water == 'fresh',], col = 'blue',
		lty = 2, lwd = 2)
# Plot the salt water data in red
lines(RelPres.mbar~RelDepth.cm, data = df[df$Water == 'salt',], col = 'red',
		lty = 1, lwd = 2)
lines(EstPres.mbar~RelDepth.cm, data = df[df$Water == 'salt',], col = 'red',
		lty = 2, lwd = 2)
legend('topleft',legend = c('Freshwater measured','Freshwater estimated',
				'Saltwater measured','Saltwater estimated'),
		col = c('blue','blue','red','red'), lty = c(1,2,1,2))